import Foundation
import OSLog

#if canImport(CloudKit)
import CloudKit

/// The database subscription a persisted CKSyncEngine state remembers.
///
/// On a fresh state CKSyncEngine adopts any existing `CKDatabaseSubscription`
/// it discovers in the private database and saves the configured
/// `subscriptionID` only when it finds none. It keeps that choice in its state
/// and never revisits it, so a later or different configured ID is ignored. A
/// leftover CloudKit E2E subscription captured every Ahoi engine this way.
///
/// This rebinds such a state to the configured subscription by changing
/// exactly two fields: the remembered ID and `needsToSaveDatabaseSubscription`.
/// Tokens, account and pending changes are kept. The old subscription is
/// neither referenced nor deleted; it may belong to another client. The
/// desktop provider applies the same rule
/// (`cloudkit_sync_subscription_mac.h`).
enum EngineSubscriptionBinding {
    struct State: Equatable {
        /// Empty: nothing remembered yet.
        var remembered: String
        var needsSave: Bool
    }

    private static let stateClassName = "CKSyncEngineState"
    static let existingKey = "existingDatabaseSubscriptionID"
    static let needsSaveKey = "needsToSaveDatabaseSubscription"

    static func read(_ serialization: CKSyncEngine.State.Serialization) -> State? {
        innerArchive(of: serialization).flatMap(probe)
    }

    static func shouldRebind(_ state: State, configured: String?) -> Bool {
        guard let configured, !configured.isEmpty else { return false }
        return !state.remembered.isEmpty && state.remembered != configured
    }

    /// A copy that remembers `configured` and asks the engine to save it, or
    /// nil when no rebind is needed, the state is not in the one recognized
    /// shape, or the result does not read back exactly as intended.
    static func rebound(
        _ serialization: CKSyncEngine.State.Serialization,
        to configured: String?
    ) -> CKSyncEngine.State.Serialization? {
        guard let configured,
              let inner = innerArchive(of: serialization),
              let before = probe(inner),
              shouldRebind(before, configured: configured),
              let rewritten = rebind(
                  inner, from: before.remembered, to: configured
              ),
              let result = wrap(rewritten),
              read(result) == State(remembered: configured, needsSave: true)
        else { return nil }
        return result
    }

    private static let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "app.ahoibrowser.AhoiBrowser",
        category: "AhoiSyncSubscription"
    )

    static func log(_ action: String) {
        logger.notice("AhoiSyncSubscription action=\(action, privacy: .public)")
    }

    /// Create-only and idempotent. The engine also saves it (needsToSave),
    /// but pushes must not depend on that private behavior alone.
    static func saveConfigured(_ subscriptionID: String, in database: CKDatabase) {
        let subscription = CKDatabaseSubscription(subscriptionID: subscriptionID)
        let info = CKSubscription.NotificationInfo()
        info.shouldSendContentAvailable = true
        subscription.notificationInfo = info
        let operation = CKModifySubscriptionsOperation(
            subscriptionsToSave: [subscription], subscriptionIDsToDelete: []
        )
        operation.qualityOfService = .utility
        operation.modifySubscriptionsResultBlock = { result in
            switch result {
            case .success:
                log("save ok=1")
            case .failure(let error):
                log("save ok=0 code=\((error as? CKError)?.errorCode ?? -1)")
            }
        }
        database.add(operation)
    }

    // MARK: - Archive access

    /// `CKSyncEngine.State.Serialization` encodes as `{"data": <archive>}`.
    private static func innerArchive(
        of serialization: CKSyncEngine.State.Serialization
    ) -> Data? {
        guard let json = try? JSONEncoder().encode(serialization),
              let object = try? JSONSerialization.jsonObject(with: json),
              let fields = object as? [String: Any],
              fields.count == 1,
              let encoded = fields["data"] as? String
        else { return nil }
        return Data(base64Encoded: encoded)
    }

    private static func wrap(_ inner: Data) -> CKSyncEngine.State.Serialization? {
        guard let json = try? JSONSerialization.data(
            withJSONObject: ["data": inner.base64EncodedString()]
        ) else { return nil }
        return try? JSONDecoder().decode(
            CKSyncEngine.State.Serialization.self, from: json
        )
    }

    /// Decodes only the two subscription fields; keyed decoding ignores every
    /// other key, so no private CloudKit class is instantiated.
    private static func probe(_ inner: Data) -> State? {
        guard let unarchiver = try? NSKeyedUnarchiver(forReadingFrom: inner)
        else { return nil }
        unarchiver.requiresSecureCoding = true
        unarchiver.decodingFailurePolicy = .setErrorAndReturn
        unarchiver.setClass(
            EngineSubscriptionProbe.self, forClassName: stateClassName
        )
        let probe = unarchiver.decodeObject(
            of: EngineSubscriptionProbe.self,
            forKey: NSKeyedArchiveRootObjectKey
        )
        unarchiver.finishDecoding()
        guard unarchiver.error == nil, let probe, probe.recognized else {
            return nil
        }
        return State(remembered: probe.remembered ?? "", needsSave: probe.needsSave)
    }

    private static func rebind(
        _ inner: Data, from old: String, to configured: String
    ) -> Data? {
        // Mutable Foundation containers keep every archive value (UIDs,
        // numbers, data) exactly as parsed; nothing is bridged and rebuilt.
        guard let archive = try? PropertyListSerialization.propertyList(
                  from: inner, options: .mutableContainers, format: nil
              ) as? NSMutableDictionary,
              archive["$archiver"] as? String == "NSKeyedArchiver",
              let objects = archive["$objects"] as? NSMutableArray
        else { return nil }
        var roots: [Int] = []
        var remembered: [Int] = []
        for index in 0..<objects.count {
            let object = objects[index]
            if let fields = object as? NSMutableDictionary,
               fields[existingKey] != nil,
               fields[needsSaveKey] is NSNumber {
                roots.append(index)
            } else if let string = object as? String, string == old {
                remembered.append(index)
            }
        }
        // Only one string may carry the old identifier, so replacing it cannot
        // change the meaning of any other field.
        guard roots.count == 1, remembered.count == 1,
              let root = objects[roots[0]] as? NSMutableDictionary
        else { return nil }
        root[needsSaveKey] = true
        objects[remembered[0]] = configured
        return try? PropertyListSerialization.data(
            fromPropertyList: archive, format: .binary, options: 0
        )
    }
}

@objc(AhoiEngineSubscriptionProbe)
private final class EngineSubscriptionProbe: NSObject, NSSecureCoding {
    static var supportsSecureCoding: Bool { true }

    let remembered: String?
    let needsSave: Bool
    let recognized: Bool

    init?(coder: NSCoder) {
        recognized = coder.containsValue(
            forKey: EngineSubscriptionBinding.needsSaveKey
        )
        remembered = coder.decodeObject(
            of: NSString.self, forKey: EngineSubscriptionBinding.existingKey
        ) as String?
        needsSave = coder.decodeBool(
            forKey: EngineSubscriptionBinding.needsSaveKey
        )
        super.init()
    }

    func encode(with coder: NSCoder) {}
}

#endif
