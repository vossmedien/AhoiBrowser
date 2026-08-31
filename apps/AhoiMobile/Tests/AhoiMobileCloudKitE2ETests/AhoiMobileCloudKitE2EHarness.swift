import CloudKit
import CoreFoundation
import CryptoKit
import Dispatch
import Foundation
import Security
import XCTest
import AhoiCloudKitSpike
@testable import AhoiMobileCore

// SecTask ships in Security on iOS but its SDK header is exposed only to the
// macOS Clang module. These test-target-only declarations use the public C ABI
// to inspect the entitlements of the hosted app process itself.
@_silgen_name("SecTaskCreateFromSelf")
private func ahoiSecTaskCreateFromSelf(_ allocator: CFAllocator?) -> CFTypeRef?

@_silgen_name("SecTaskCopyValueForEntitlement")
private func ahoiSecTaskCopyValueForEntitlement(
    _ task: CFTypeRef,
    _ entitlement: CFString,
    _ error: UnsafeMutablePointer<Unmanaged<CFError>?>?
) -> CFTypeRef?

@available(iOS 17.0, *)
enum AhoiMobileCloudKitE2EHarness {
    static let hostBundleIdentifier = "app.ahoibrowser.AhoiBrowser"
    static let containerIdentifier = "iCloud.app.ahoibrowser.AhoiBrowser"
    static let buildMode = "CloudKitDevelopment"
    static let teamIdentifier = "248AJ5BN47"
    static let zonePrefix = "AhoiBrowserCloudKitE2E."
    static let subscriptionPrefix = "app.ahoibrowser.AhoiBrowser.cloudkit-e2e."

    struct HostContract {
        let containerIdentifier: String
        let runScope: RunScope?
    }

    struct RunScope: Sendable {
        let token: UUID
        let zoneName: String
        let subscriptionID: String
        let ownerRecordID: UUID
        let payloadRecordID: UUID
        let deviceID: DeviceID

        var zoneID: CKRecordZone.ID {
            CKRecordZone.ID(
                zoneName: zoneName,
                ownerName: CKCurrentUserDefaultName
            )
        }

        var ownerPlaintext: Data {
            Data("ahoi-cloudkit-e2e-owner:\(token.uuidString.lowercased())".utf8)
        }

        init(token: UUID) throws {
            let compact = token.uuidString
                .replacingOccurrences(of: "-", with: "")
                .lowercased()
            let zoneName = AhoiMobileCloudKitE2EHarness.zonePrefix + compact
            let subscriptionID =
                AhoiMobileCloudKitE2EHarness.subscriptionPrefix + compact
            let ownerRecordID = Self.derivedUUID(token: token, label: "owner")
            let payloadRecordID = Self.derivedUUID(token: token, label: "payload")
            let deviceUUID = Self.derivedUUID(token: token, label: "device")
            guard compact.count == 32,
                  zoneName.hasPrefix(AhoiMobileCloudKitE2EHarness.zonePrefix),
                  zoneName.hasSuffix(compact),
                  subscriptionID.hasPrefix(
                    AhoiMobileCloudKitE2EHarness.subscriptionPrefix
                  ),
                  subscriptionID.hasSuffix(compact),
                  ownerRecordID != payloadRecordID,
                  ownerRecordID != deviceUUID,
                  payloadRecordID != deviceUUID else {
                throw HarnessError.invalidRunScope
            }
            self.token = token
            self.zoneName = zoneName
            self.subscriptionID = subscriptionID
            self.ownerRecordID = ownerRecordID
            self.payloadRecordID = payloadRecordID
            self.deviceID = DeviceID(rawValue: deviceUUID)
        }

        private static func derivedUUID(token: UUID, label: String) -> UUID {
            var bytes = Array(SHA256.hash(
                data: Data("\(token.uuidString.lowercased()):\(label)".utf8)
            ).prefix(16))
            bytes[6] = (bytes[6] & 0x0F) | 0x50
            bytes[8] = (bytes[8] & 0x3F) | 0x80
            return UUID(uuid: (
                bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]
            ))
        }
    }

    enum HarnessError: Error, LocalizedError {
        case hostedModeMissing
        case unexpectedBundleIdentifier(String?)
        case unexpectedBuildMode(String?)
        case unexpectedContainerConfiguration(String?)
        case missingEntitlement(String, String)
        case unexpectedStringEntitlement(String, String?)
        case unexpectedArrayEntitlement(String, [String])
        case invalidMutationRunToken(String?)
        case invalidRunScope
        case scopeFreshnessReadFailed(String)
        case scopeFreshnessTimedOut
        case reusedRunScope(zoneExists: Bool, subscriptionExists: Bool)
        case missingServerRecord(String)
        case ownershipMarkerMismatch
        case malformedCiphertext

        var errorDescription: String? {
            switch self {
            case .hostedModeMissing:
                "The CloudKit E2E target is not running in its inert hosted-test mode."
            case .unexpectedBundleIdentifier(let value):
                "Unexpected hosted app bundle identifier: \(value ?? "nil")"
            case .unexpectedBuildMode(let value):
                "Unexpected AhoiBuildMode: \(value ?? "nil")"
            case .unexpectedContainerConfiguration(let value):
                "Unexpected configured CloudKit container: \(value ?? "nil")"
            case .missingEntitlement(let key, let detail):
                "Missing signed entitlement \(key): \(detail)"
            case .unexpectedStringEntitlement(let key, let value):
                "Unexpected signed entitlement \(key): \(value ?? "nil")"
            case .unexpectedArrayEntitlement(let key, let value):
                "Unexpected signed entitlement \(key): \(value)"
            case .invalidMutationRunToken(let value):
                "The real-mutation run token is not a valid UUID: \(value ?? "nil")"
            case .invalidRunScope:
                "The UUID-derived CloudKit E2E scope is invalid."
            case .scopeFreshnessReadFailed(let detail):
                "The CloudKit E2E scope freshness check failed: \(detail)"
            case .scopeFreshnessTimedOut:
                "The read-only CloudKit E2E scope freshness check timed out."
            case .reusedRunScope(let zoneExists, let subscriptionExists):
                "The CloudKit E2E run token is not fresh " +
                    "(zoneExists=\(zoneExists), " +
                    "subscriptionExists=\(subscriptionExists))."
            case .missingServerRecord(let recordName):
                "CloudKit did not return the expected record \(recordName)."
            case .ownershipMarkerMismatch:
                "The encrypted CloudKit E2E ownership marker does not match this run."
            case .malformedCiphertext:
                "The fetched AES-GCM envelope is malformed."
            }
        }
    }

    static func validateSignedHost(
        requireRealMutation: Bool
    ) throws -> HostContract {
        let environment = ProcessInfo.processInfo.environment
        let hosted = environment["AHOI_CLOUDKIT_E2E_HOST_MODE"] == "1" && (
            environment["XCTestConfigurationFilePath"] != nil ||
            environment["XCInjectBundleInto"] != nil ||
            NSClassFromString("XCTest.XCTestCase") != nil
        )
        guard hosted else { throw HarnessError.hostedModeMissing }

        let bundle = Bundle.main
        guard bundle.bundleIdentifier == hostBundleIdentifier else {
            throw HarnessError.unexpectedBundleIdentifier(bundle.bundleIdentifier)
        }
        let configuredMode = configuredString(
            bundle.object(forInfoDictionaryKey: "AhoiBuildMode")
        )
        guard configuredMode == buildMode else {
            throw HarnessError.unexpectedBuildMode(configuredMode)
        }
        let configuredContainer = configuredString(
            bundle.object(forInfoDictionaryKey: "AHOI_CLOUDKIT_CONTAINER_ID")
        )
        guard configuredContainer == containerIdentifier else {
            throw HarnessError.unexpectedContainerConfiguration(configuredContainer)
        }

        let signedApplicationIdentifier = try stringEntitlement(
            "application-identifier"
        )
        guard signedApplicationIdentifier ==
            "\(teamIdentifier).\(hostBundleIdentifier)" else {
            throw HarnessError.unexpectedStringEntitlement(
                "application-identifier",
                signedApplicationIdentifier
            )
        }
        let signedTeamIdentifier = try stringEntitlement(
            "com.apple.developer.team-identifier"
        )
        guard signedTeamIdentifier == teamIdentifier else {
            throw HarnessError.unexpectedStringEntitlement(
                "com.apple.developer.team-identifier",
                signedTeamIdentifier
            )
        }

        let containerEntitlement = "com.apple.developer.icloud-container-identifiers"
        let signedContainers = try stringArrayEntitlement(containerEntitlement)
        guard signedContainers == [containerIdentifier] else {
            throw HarnessError.unexpectedArrayEntitlement(
                containerEntitlement,
                signedContainers
            )
        }
        let serviceEntitlement = "com.apple.developer.icloud-services"
        let signedServices = try stringArrayEntitlement(serviceEntitlement)
        guard Set(signedServices) == Set(["CloudKit"]) else {
            throw HarnessError.unexpectedArrayEntitlement(
                serviceEntitlement,
                signedServices
            )
        }
        let environmentEntitlement =
            "com.apple.developer.icloud-container-environment"
        let signedEnvironment = try stringEntitlement(environmentEntitlement)
        guard signedEnvironment == "Development" else {
            throw HarnessError.unexpectedStringEntitlement(
                environmentEntitlement,
                signedEnvironment
            )
        }

        guard requireRealMutation else {
            return HostContract(
                containerIdentifier: containerIdentifier,
                runScope: nil
            )
        }
        let mutationOptIn = configuredString(bundle.object(
            forInfoDictionaryKey: "AhoiCloudKitE2ERealMutationOptIn"
        ))
        guard mutationOptIn == "YES" else {
            throw XCTSkip(
                "Real CloudKit mutation is disabled. Pass " +
                    "AHOI_CLOUDKIT_E2E_REAL_MUTATION_OPT_IN=YES and a fresh " +
                    "AHOI_CLOUDKIT_E2E_RUN_TOKEN UUID to xcodebuild."
            )
        }
        let rawToken = configuredString(bundle.object(
            forInfoDictionaryKey: "AhoiCloudKitE2ERunToken"
        ))
        guard let rawToken, let token = UUID(uuidString: rawToken) else {
            throw HarnessError.invalidMutationRunToken(rawToken)
        }
        return HostContract(
            containerIdentifier: containerIdentifier,
            runScope: try RunScope(token: token)
        )
    }

    static func makeProvider(
        contract: HostContract,
        scope: RunScope,
        createsSubscription: Bool = false,
        recordStore: any LocalSyncRecordStore = InMemorySyncRecordStore()
    ) throws -> CloudKitSyncProvider {
        if createsSubscription {
            guard contract.runScope?.token == scope.token else {
                throw HarnessError.invalidRunScope
            }
            try assertFreshScope(
                containerIdentifier: contract.containerIdentifier,
                scope: scope
            )
        }
        return try CloudKitSyncProvider(
            configuration: .init(
                containerIdentifier: contract.containerIdentifier,
                zoneName: scope.zoneName,
                automaticallySync: false,
                subscriptionID: createsSubscription ? scope.subscriptionID : nil
            ),
            recordStore: recordStore,
            stateStore: InMemorySyncEngineStateStore(),
            quarantineStore: InMemorySyncQuarantineStore(),
            systemFieldsStore: InMemoryCloudKitSystemFieldsStore()
        )
    }

    static func makeRecord(
        recordID: UUID,
        entityID: UUID,
        dataClass: SyncDataClass,
        deviceID: DeviceID,
        clock: HybridLogicalClock,
        plaintext: Data,
        key: SymmetricKey,
        tombstone: Tombstone? = nil
    ) throws -> SyncRecord {
        let sealed = try AES.GCM.seal(plaintext, using: key)
        return SyncRecord(
            recordID: recordID,
            entityID: entityID,
            dataClass: dataClass,
            modifiedAt: clock,
            originatingDevice: deviceID,
            encryptedValue: .init(
                keyVersion: 1,
                nonce: sealed.nonce.withUnsafeBytes { Data($0) },
                ciphertextAndTag: sealed.ciphertext + sealed.tag
            ),
            tombstone: tombstone
        )
    }

    static func open(_ value: EncryptedValue, using key: SymmetricKey) throws -> Data {
        guard value.algorithm == .aes256GCM,
              value.keyVersion == 1,
              value.nonce.count == 12,
              value.ciphertextAndTag.count >= 16 else {
            throw HarnessError.malformedCiphertext
        }
        let tagOffset = value.ciphertextAndTag.count - 16
        let box = try AES.GCM.SealedBox(
            nonce: AES.GCM.Nonce(data: value.nonce),
            ciphertext: value.ciphertextAndTag.prefix(tagOffset),
            tag: value.ciphertextAndTag.suffix(16)
        )
        return try AES.GCM.open(box, using: key)
    }

    static func fetchServerRecord(
        database: CKDatabase,
        recordID: CKRecord.ID
    ) async throws -> CKRecord {
        let results = try await database.records(for: [recordID])
        guard let result = results[recordID] else {
            throw HarnessError.missingServerRecord(recordID.recordName)
        }
        return try result.get()
    }

    /// The writer performs this read-only check before it can construct a
    /// provider and call `prepare()`. A UUID is therefore not treated as fresh
    /// merely because it is syntactically valid. Existing scoped server state
    /// is never adopted, overwritten, or cleaned by a later run.
    private static func assertFreshScope(
        containerIdentifier: String,
        scope: RunScope
    ) throws {
        let database = CKContainer(
            identifier: containerIdentifier
        ).privateCloudDatabase
        let probe = AhoiMobileCloudKitE2EScopeFreshnessProbe()
        database.fetchAllRecordZones { zones, error in
            probe.completeZones(zones, error: error)
        }
        database.fetchAllSubscriptions { subscriptions, error in
            probe.completeSubscriptions(subscriptions, error: error)
        }
        let snapshot = try probe.wait(timeout: 30)
        let zoneExists = snapshot.zones.contains { $0.zoneID == scope.zoneID }
        let subscriptionExists = snapshot.subscriptions.contains {
            $0.subscriptionID == scope.subscriptionID
        }
        guard !zoneExists, !subscriptionExists else {
            throw HarnessError.reusedRunScope(
                zoneExists: zoneExists,
                subscriptionExists: subscriptionExists
            )
        }
    }

    private static func configuredString(_ value: Any?) -> String? {
        guard let string = value as? String else { return nil }
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !trimmed.contains("$(") else { return nil }
        return trimmed
    }

    private static func stringArrayEntitlement(_ key: String) throws -> [String] {
        let value = try entitlementValue(key)
        guard let strings = value as? [String] else {
            throw HarnessError.unexpectedArrayEntitlement(key, [])
        }
        return strings.sorted()
    }

    private static func stringEntitlement(_ key: String) throws -> String? {
        try entitlementValue(key) as? String
    }

    private static func entitlementValue(_ key: String) throws -> Any {
        guard let task = ahoiSecTaskCreateFromSelf(nil) else {
            throw HarnessError.missingEntitlement(key, "SecTask unavailable")
        }
        var copyError: Unmanaged<CFError>?
        guard let value = ahoiSecTaskCopyValueForEntitlement(
            task,
            key as CFString,
            &copyError
        ) else {
            let detail = copyError.map {
                String(describing: $0.takeRetainedValue())
            } ?? "value absent"
            throw HarnessError.missingEntitlement(key, detail)
        }
        return value
    }
}

@available(iOS 17.0, *)
private final class AhoiMobileCloudKitE2EScopeFreshnessProbe: @unchecked Sendable {
    struct Snapshot {
        let zones: [CKRecordZone]
        let subscriptions: [CKSubscription]
    }

    private let condition = NSCondition()
    private var zonesResult: Result<[CKRecordZone], Error>?
    private var subscriptionsResult: Result<[CKSubscription], Error>?

    func completeZones(_ zones: [CKRecordZone]?, error: Error?) {
        condition.withLock {
            zonesResult = Self.result(
                value: zones,
                error: error,
                missingDetail: "record-zone response was empty"
            )
            condition.broadcast()
        }
    }

    func completeSubscriptions(_ subscriptions: [CKSubscription]?, error: Error?) {
        condition.withLock {
            subscriptionsResult = Self.result(
                value: subscriptions,
                error: error,
                missingDetail: "subscription response was empty"
            )
            condition.broadcast()
        }
    }

    func wait(timeout: TimeInterval) throws -> Snapshot {
        let deadline = Date(timeIntervalSinceNow: timeout)
        return try condition.withLock {
            while zonesResult == nil || subscriptionsResult == nil {
                guard condition.wait(until: deadline) else {
                    throw AhoiMobileCloudKitE2EHarness.HarnessError
                        .scopeFreshnessTimedOut
                }
            }
            guard let zonesResult, let subscriptionsResult else {
                throw AhoiMobileCloudKitE2EHarness.HarnessError
                    .scopeFreshnessTimedOut
            }
            return Snapshot(
                zones: try zonesResult.get(),
                subscriptions: try subscriptionsResult.get()
            )
        }
    }

    private static func result<Value>(
        value: Value?,
        error: Error?,
        missingDetail: String
    ) -> Result<Value, Error> {
        if let error {
            return .failure(
                AhoiMobileCloudKitE2EHarness.HarnessError
                    .scopeFreshnessReadFailed(error.localizedDescription)
            )
        }
        guard let value else {
            return .failure(
                AhoiMobileCloudKitE2EHarness.HarnessError
                    .scopeFreshnessReadFailed(missingDetail)
            )
        }
        return .success(value)
    }
}

@available(iOS 17.0, *)
final class AhoiMobileCloudKitE2ECleanupOwner:
    NSObject,
    @unchecked Sendable,
    XCTestObservation {
    private struct CleanupAttempt {
        let id: UUID
        let task: Task<Void, Error>
    }

    private let containerIdentifier: String
    private let scope: AhoiMobileCloudKitE2EHarness.RunScope
    private let key: SymmetricKey
    private let stateLock = NSLock()
    private var providers: [ObjectIdentifier: CloudKitSyncProvider] = [:]
    private var cleanupAttempt: CleanupAttempt?
    private var didFinish = false
    private var isObserving = true

    init(
        containerIdentifier: String,
        scope: AhoiMobileCloudKitE2EHarness.RunScope,
        key: SymmetricKey
    ) {
        self.containerIdentifier = containerIdentifier
        self.scope = scope
        self.key = key
        super.init()
        AhoiMobileCloudKitE2ECleanupRegistry.shared.add(self)
        withXCTestObservationCenterOnMainThread {
            XCTestObservationCenter.shared.addTestObserver(self)
        }
    }

    func register(_ provider: CloudKitSyncProvider) {
        let cleanupAlreadyStarted = stateLock.withLock {
            guard !didFinish, cleanupAttempt == nil else { return true }
            providers[ObjectIdentifier(provider)] = provider
            return false
        }
        if cleanupAlreadyStarted {
            _ = Task.detached(priority: .utility) {
                await provider.cancel()
            }
        }
    }

    func stop(_ provider: CloudKitSyncProvider) async {
        await provider.cancel()
        stateLock.withLock {
            _ = providers.removeValue(forKey: ObjectIdentifier(provider))
        }
    }

    func cleanup() async throws {
        guard let attempt = beginCleanupAttempt() else { return }
        do {
            try await attempt.task.value
            finishCleanupAttempt(attempt.id, succeeded: true)
        } catch {
            finishCleanupAttempt(attempt.id, succeeded: false)
            throw error
        }
    }

    /// XCTest invokes this even when the async test unwinds through a failure or
    /// cancellation path. Normal explicit cleanup removes the observer before
    /// this callback. The bounded wait keeps the test process alive while the
    /// detached cleanup task ignores caller cancellation and finishes.
    func testCaseDidFinish(_ testCase: XCTestCase) {
        guard needsFallbackCleanup else {
            unregisterObservation()
            return
        }
        let completion = AhoiMobileCloudKitE2ETeardownCompletion()
        _ = Task.detached(priority: .utility) { [self] in
            do {
                try await cleanup()
                completion.complete(.success)
            } catch {
                completion.complete(.failure(error.localizedDescription))
            }
        }
        let outcome = completion.wait(timeout: 60)
        switch outcome {
        case .success:
            return
        case .failure(let detail):
            recordFallbackFailure(
                "CloudKit E2E teardown could not prove ownership or clean its " +
                    "synthetic scope: \(detail). No unverified delete was attempted.",
                on: testCase
            )
        case nil:
            recordFallbackFailure(
                "CloudKit E2E teardown timed out. Its detached cleanup may still " +
                    "finish, but no unverified delete was attempted.",
                on: testCase
            )
        }
        unregisterObservation()
    }

    private var needsFallbackCleanup: Bool {
        stateLock.withLock { !didFinish }
    }

    private func beginCleanupAttempt() -> CleanupAttempt? {
        stateLock.withLock {
            guard !didFinish else { return nil }
            if let cleanupAttempt { return cleanupAttempt }
            let attemptID = UUID()
            let activeProviders = Array(providers.values)
            providers.removeAll(keepingCapacity: false)
            let containerIdentifier = self.containerIdentifier
            let scope = self.scope
            let key = self.key
            let task = Task.detached(priority: .utility) {
                try await Self.performCleanup(
                    activeProviders: activeProviders,
                    containerIdentifier: containerIdentifier,
                    scope: scope,
                    key: key
                )
            }
            let attempt = CleanupAttempt(id: attemptID, task: task)
            cleanupAttempt = attempt
            return attempt
        }
    }

    private func finishCleanupAttempt(_ id: UUID, succeeded: Bool) {
        let shouldUnregister = stateLock.withLock {
            guard cleanupAttempt?.id == id else { return false }
            cleanupAttempt = nil
            if succeeded { didFinish = true }
            return succeeded
        }
        if shouldUnregister {
            unregisterObservation()
        }
    }

    private static func performCleanup(
        activeProviders: [CloudKitSyncProvider],
        containerIdentifier: String,
        scope: AhoiMobileCloudKitE2EHarness.RunScope,
        key: SymmetricKey
    ) async throws {
        for provider in activeProviders {
            await provider.cancel()
        }
        let database = CKContainer(
            identifier: containerIdentifier
        ).privateCloudDatabase

        // No destructive call is permitted until the exact scoped marker has
        // been fetched, decoded and authenticated with this run's in-memory key.
        let markerID = CKRecord.ID(
            recordName: scope.ownerRecordID.uuidString.lowercased(),
            zoneID: scope.zoneID
        )
        let marker = try await AhoiMobileCloudKitE2EHarness.fetchServerRecord(
            database: database,
            recordID: markerID
        )
        let decoded = try AppleCloudKitRecordCodec().decode(marker)
        guard marker.recordType == AppleCloudKitRecordCodec.recordType,
              decoded.recordID == scope.ownerRecordID,
              decoded.entityID == scope.ownerRecordID,
              decoded.dataClass == .recoveryMetadata,
              decoded.tombstone == nil,
              try AhoiMobileCloudKitE2EHarness.open(
                decoded.encryptedValue,
                using: key
              ) == scope.ownerPlaintext else {
            throw AhoiMobileCloudKitE2EHarness.HarnessError.ownershipMarkerMismatch
        }

        do {
            _ = try await database.deleteSubscription(withID: scope.subscriptionID)
        } catch let error as CKError where error.code == .unknownItem {
            // The verified run may not have reached subscription creation.
        }
        _ = try await database.deleteRecordZone(withID: scope.zoneID)
    }

    private func unregisterObservation() {
        let shouldUnregister = stateLock.withLock {
            guard isObserving else { return false }
            isObserving = false
            return true
        }
        guard shouldUnregister else { return }
        withXCTestObservationCenterOnMainThread {
            XCTestObservationCenter.shared.removeTestObserver(self)
        }
        AhoiMobileCloudKitE2ECleanupRegistry.shared.remove(self)
    }

    /// XCTest requires observer mutations on the process main thread even
    /// when an async test resumes on its cooperative executor. Keep the
    /// registration synchronous so cleanup is armed before any CloudKit write.
    private func withXCTestObservationCenterOnMainThread(
        _ action: @escaping @Sendable () -> Void
    ) {
        if Thread.isMainThread {
            action()
        } else {
            DispatchQueue.main.sync(execute: action)
        }
    }

    private func recordFallbackFailure(
        _ message: String,
        on testCase: XCTestCase
    ) {
        testCase.record(XCTIssue(
            type: .assertionFailure,
            compactDescription: message
        ))
        FileHandle.standardError.write(Data((message + "\n").utf8))
    }
}

@available(iOS 17.0, *)
private final class AhoiMobileCloudKitE2ECleanupRegistry: @unchecked Sendable {
    static let shared = AhoiMobileCloudKitE2ECleanupRegistry()

    private let lock = NSLock()
    private var owners: [
        ObjectIdentifier: AhoiMobileCloudKitE2ECleanupOwner
    ] = [:]

    func add(_ owner: AhoiMobileCloudKitE2ECleanupOwner) {
        lock.withLock {
            owners[ObjectIdentifier(owner)] = owner
        }
    }

    func remove(_ owner: AhoiMobileCloudKitE2ECleanupOwner) {
        lock.withLock {
            _ = owners.removeValue(forKey: ObjectIdentifier(owner))
        }
    }
}

@available(iOS 17.0, *)
private final class AhoiMobileCloudKitE2ETeardownCompletion: @unchecked Sendable {
    enum Outcome {
        case success
        case failure(String)
    }

    private let lock = NSLock()
    private let semaphore = DispatchSemaphore(value: 0)
    private var outcome: Outcome?

    func complete(_ outcome: Outcome) {
        lock.withLock {
            guard self.outcome == nil else { return }
            self.outcome = outcome
            semaphore.signal()
        }
    }

    func wait(timeout: TimeInterval) -> Outcome? {
        guard semaphore.wait(timeout: .now() + timeout) == .success else {
            return nil
        }
        return lock.withLock { outcome }
    }
}
