import AhoiCloudKitSpike

/// Explicit local user intent, not a wire record or a publication request.
public enum MobileSharedTabIntent: Sendable {
    case navigate(SharedTabTarget)
    case move(WorkspaceID?)
    case rename(String?)
}
