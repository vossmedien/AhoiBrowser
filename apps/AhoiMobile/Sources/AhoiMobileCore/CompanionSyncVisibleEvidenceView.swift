import SwiftUI

struct CompanionSyncVisibleEvidenceView: View {
    let evidence: CompanionSyncVisibleEvidence

    var body: some View {
        evidenceRow(
            "Current-session normal tabs",
            value: String(evidence.currentSessionOpenTabCount),
            identifier: "settings.sync.evidence.current-session-tabs"
        )
        evidenceRow(
            "Current-session tab outbox",
            value: String(evidence.currentSessionOutboxTabCount),
            identifier: "settings.sync.evidence.outbox-tabs"
        )
        evidenceRow(
            "History outbox",
            value: String(evidence.historyOutboxCount),
            identifier: "settings.sync.evidence.history"
        )
        evidenceRow(
            "Pending encrypted records",
            value: String(evidence.pendingRecordCount),
            identifier: "settings.sync.evidence.pending"
        )
        evidenceRow(
            "Payload encryption",
            value: evidence.encryptionState,
            identifier: "settings.sync.evidence.encrypted"
        )
        evidenceRow(
            "Denied private records",
            value: String(evidence.deniedRecordCount),
            identifier: "settings.sync.evidence.denied"
        )
        evidenceRow(
            "Field conflict",
            value: evidence.conflictState.rawValue,
            identifier: "settings.sync.evidence.conflict"
        )
    }

    private func evidenceRow(
        _ label: String,
        value: String,
        identifier: String
    ) -> some View {
        LabeledContent(label, value: value)
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier(identifier)
            .accessibilityValue(Text(value))
    }
}
