import AhoiCloudKitSpike
import Foundation

/// Explicit local user intent, not a wire record or a publication request.
public enum MobileSharedTabIntent: Codable, Equatable, Sendable {
    case navigate(SharedTabTarget)
    case move(WorkspaceID?)
    case rename(String?)
}

/// Local durable intent, never a wire record/creator identity. At most the
/// latest navigation, location and title intention need to survive a deferred
/// domain binding. UUID acknowledgments cannot consume a newer user action.
public struct MobileSharedTabMutation: Codable, Equatable, Sendable {
    public let id: UUID
    public let intent: MobileSharedTabIntent

    var field: String {
        switch intent {
        case .navigate: return "url"
        case .move: return "location"
        case .rename: return "title"
        }
    }

    var isValid: Bool {
        guard MobileTabRecord.isNonzeroUUID(id) else { return false }
        switch intent {
        case .navigate(let target): return (try? target.validatePage(isTemporary: true)) != nil
        case .move(let workspace): return workspace.map { MobileTabRecord.isNonzeroUUID($0.rawValue) } ?? true
        case .rename(let title): return title.map { $0.utf8.count <= MobileTabRecord.maximumTitleUTF8Bytes } ?? true
        }
    }
}

extension MobileTabRecord {
    mutating func stageSharedIntent(_ intent: MobileSharedTabIntent) {
        guard mode == .normal else { return }
        let resolved: MobileSharedTabIntent
        if case .rename(nil) = intent { resolved = .rename(title) }
        else { resolved = intent }
        let mutation = MobileSharedTabMutation(id: UUID(), intent: resolved)
        guard mutation.isValid else { return }
        pendingSharedMutations.removeAll { $0.field == mutation.field }
        pendingSharedMutations.append(mutation)
    }
}
