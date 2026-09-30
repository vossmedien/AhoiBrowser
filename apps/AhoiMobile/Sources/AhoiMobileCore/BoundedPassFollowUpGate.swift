import Foundation

/// Decides whether a bounded pass that ended with leftovers asks the host for
/// another pass. A follow-up is worth one attempt for a new set of leftovers
/// (a merge produced a record, a send conflicted). Leftovers that the previous
/// pass already failed to change — a Presence waiting for its Page, a fetched
/// record whose dependency has not arrived, a deferred developer asset — stay
/// visible as "retry scheduled" but do not restart the pass by themselves;
/// only a genuinely new remote push or local change does.
struct BoundedPassFollowUpGate: Sendable, Equatable {
    private var unresolvedLeftovers: Set<String>?

    mutating func shouldRequestFollowUp(leftovers: Set<String>) -> Bool {
        defer { unresolvedLeftovers = leftovers }
        return leftovers != unresolvedLeftovers
    }

    mutating func passCompleted() {
        unresolvedLeftovers = nil
    }
}
