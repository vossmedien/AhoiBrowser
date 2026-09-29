import Foundation
import AhoiCloudKitSpike

public enum CompanionSyncVisibleConflictState: String, Equatable, Sendable {
    case notRequested = "not-requested"
    case pending
    case resolved
    case failed
}

/// Privacy-safe counters exposed only when the explicit DEBUG projection is
/// active. The values are derived from the real repository, encrypted outbox,
/// sync transport pending state, and field-merge path; no page metadata is shown.
public struct CompanionSyncVisibleEvidence: Equatable, Sendable {
    public let currentSessionOpenTabCount: Int
    public let currentSessionOutboxTabCount: Int
    public let historyOutboxCount: Int
    public let pendingRecordCount: Int
    public let encryptedRecordCount: Int
    public let totalOutboxRecordCount: Int
    public let deniedRecordCount: Int
    public let conflictState: CompanionSyncVisibleConflictState

    public var encryptionState: String {
        totalOutboxRecordCount > 0 && encryptedRecordCount == totalOutboxRecordCount
            ? "verified"
            : "failed"
    }
}

extension CompanionAppModel {
    func refreshSyncVisibleUITestEvidenceIfNeeded() async {
#if DEBUG
        guard let runtime = syncVisibleUITestRuntime else { return }
        do {
            let records = try await runtime.recordStore.allRecords()
            let snapshot = try await repository.currentSnapshot()
            let currentTabs = snapshot.remoteTabs.filter {
                $0.sessionID == mobileSessionID && $0.context == .normal &&
                    $0.isOpen && !$0.isDeleted
            }
            let currentTabIDs = Set(currentTabs.map { $0.id.rawValue })
            let deniedClassNames: Set<String> = [
                SyncDataClass.cookie.rawValue,
                SyncDataClass.password.rawValue,
                SyncDataClass.autofill.rawValue,
                SyncDataClass.siteData.rawValue,
                SyncDataClass.cache.rawValue,
                SyncDataClass.permission.rawValue,
                SyncDataClass.extensionStorage.rawValue,
                SyncDataClass.incognito.rawValue,
                SyncDataClass.keychainSecret.rawValue,
                SyncDataClass.headerSecret.rawValue,
                SyncDataClass.httpAuthSecret.rawValue,
            ]
            let encryptedCount = records.lazy.filter {
                $0.encryptedValue.algorithm == .aes256GCM &&
                    $0.encryptedValue.nonce.count == 12 &&
                    $0.encryptedValue.ciphertextAndTag.count >= 16
            }.count
            syncVisibleEvidence = .init(
                currentSessionOpenTabCount: currentTabs.count,
                currentSessionOutboxTabCount: records.lazy.filter {
                    $0.dataClass == .deviceTab && currentTabIDs.contains($0.recordID)
                }.count,
                historyOutboxCount: records.lazy.filter {
                    $0.dataClass == .historyVisit
                }.count,
                pendingRecordCount: runtime.transport.pendingRecordCount(),
                encryptedRecordCount: encryptedCount,
                totalOutboxRecordCount: records.count,
                deniedRecordCount: records.lazy.filter {
                    deniedClassNames.contains($0.dataClass.rawValue)
                }.count,
                conflictState: runtime.conflictState
            )
        } catch {
            runtime.conflictState = runtime.conflictRequested ? .failed : .notRequested
            syncVisibleEvidence = .init(
                currentSessionOpenTabCount: 0,
                currentSessionOutboxTabCount: 0,
                historyOutboxCount: 0,
                pendingRecordCount: 0,
                encryptedRecordCount: 0,
                totalOutboxRecordCount: 0,
                deniedRecordCount: 0,
                conflictState: runtime.conflictState
            )
            presentOperationFailure(error)
        }
#endif
    }

    func clearSyncVisibleUITestRuntime() {
#if DEBUG
        syncVisibleUITestRuntime = nil
#endif
        syncVisibleEvidence = nil
    }

    func loadSyncVisibleUITestConflictIfRequested() async {
#if DEBUG
        guard let runtime = syncVisibleUITestRuntime,
              runtime.conflictRequested,
              !runtime.didAttemptConflict else { return }
        runtime.didAttemptConflict = true
        runtime.conflictState = .pending
        await refreshSyncVisibleUITestEvidenceIfNeeded()

        do {
            let currentSnapshot = try await repository.currentSnapshot()
            guard var incoming = currentSnapshot.remoteTabs.first(where: {
                $0.sessionID == mobileSessionID && $0.context == .normal &&
                    $0.isOpen && !$0.isDeleted
            }) else {
                runtime.conflictState = .failed
                await refreshSyncVisibleUITestEvidenceIfNeeded()
                return
            }
            let nextPhysical = incoming.version.modifiedAt.physicalMilliseconds == UInt64.max
                ? incoming.version.modifiedAt.physicalMilliseconds
                : incoming.version.modifiedAt.physicalMilliseconds + 1
            let remoteClock = try incoming.version.modifiedAt.ticking(
                at: nextPhysical
            )
            var fieldVersions = incoming.version.fieldVersions
            fieldVersions["title"] = remoteClock
            incoming.title = CompanionSyncVisibleUITestRuntime.conflictWinnerTitle
            incoming.version = SyncVersion(
                schemaVersion: incoming.version.schemaVersion,
                modifiedAt: remoteClock,
                modifiedBy: incoming.deviceID,
                fieldVersions: fieldVersions
            )
            let record = try runtime.payloadCodec.makeRecord(
                recordID: incoming.id.rawValue,
                entityID: incoming.id.rawValue,
                dataClass: .deviceTab,
                version: incoming.version,
                plaintext: runtime.wireCodec.encode(incoming),
                tombstone: incoming.tombstone
            )
            try await runtime.recordStore.stageFetchedRecords([record])
            try await runtime.bridge.importFetchedRecords()
            try await refreshLocalState()
            runtime.conflictState = snapshot.remoteTabs.contains {
                $0.id == incoming.id &&
                    $0.title == CompanionSyncVisibleUITestRuntime.conflictWinnerTitle
            } ? .resolved : .failed
        } catch {
            runtime.conflictState = .failed
            presentOperationFailure(error)
        }
        await refreshSyncVisibleUITestEvidenceIfNeeded()
#endif
    }
}

#if DEBUG
public enum CompanionSyncVisibleUITestLaunch {
    public static let argument = "-AhoiUITestSyncProjection"

    public static func isRequested(arguments: [String]) -> Bool {
        arguments.contains(argument)
    }
}

@MainActor
final class CompanionSyncVisibleUITestRuntime {
    static let conflictArgument = "-AhoiUITestSyncConflict"
    static let conflictWinnerTitle = "Sync conflict resolved"

    let transport: CompanionSyncVisibleTestTransport
    let bridge: CompanionSyncBridge
    let recordStore: InMemorySyncRecordStore
    let payloadCodec: CompanionPayloadCodec
    let wireCodec = DesktopWirePayloadCodec()
    let conflictRequested: Bool
    var didAttemptConflict = false
    var conflictState: CompanionSyncVisibleConflictState

    init(repository: LocalFirstRepository, arguments: [String]) throws {
        let recordStore = InMemorySyncRecordStore()
        let transport = CompanionSyncVisibleTestTransport(recordStore: recordStore)
        let keyConfiguration = CompanionSyncKeyConfiguration(
            service: "app.ahoibrowser.AhoiBrowser.ui-test",
            account: "sync-visible-e2e",
            keyVersion: 1
        )
        let sealer = KeychainCompanionPayloadSealer(
            configuration: keyConfiguration,
            keyLoader: { Data(repeating: 0xA7, count: 32) }
        )
        self.transport = transport
        self.recordStore = recordStore
        self.payloadCodec = CompanionPayloadCodec(sealer: sealer)
        self.bridge = CompanionSyncBridge(
            repository: repository,
            transport: transport,
            sealer: sealer
        )
        self.conflictRequested = arguments.contains(Self.conflictArgument)
        self.conflictState = conflictRequested ? .pending : .notRequested
    }
}

extension CompanionAppModel {
    func configureSyncVisibleUITestRuntimeIfRequested() {
        let arguments = ProcessInfo.processInfo.arguments
        guard CompanionSyncVisibleUITestLaunch.isRequested(arguments: arguments) else {
            return
        }
        do {
            let runtime = try CompanionSyncVisibleUITestRuntime(
                repository: repository,
                arguments: arguments
            )
            syncVisibleUITestRuntime = runtime
            syncBridge = runtime.bridge
            isSyncConfigured = true
            desiredSyncEnabled = true
            keyLifecycleStatus = .ready(keyVersion: 1)
            syncStatus = runtime.transport.status()
            loadError = nil
        } catch {
            syncVisibleEvidence = .init(
                currentSessionOpenTabCount: 0,
                currentSessionOutboxTabCount: 0,
                historyOutboxCount: 0,
                pendingRecordCount: 0,
                encryptedRecordCount: 0,
                totalOutboxRecordCount: 0,
                deniedRecordCount: 0,
                conflictState: .failed
            )
            presentOperationFailure(error)
        }
    }
}
#endif
