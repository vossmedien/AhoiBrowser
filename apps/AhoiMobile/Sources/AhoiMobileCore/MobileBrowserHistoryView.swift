import SwiftUI
import AhoiCloudKitSpike

public struct MobileBrowserHistoryView: View {
    @ObservedObject private var model: CompanionAppModel
    private let openURL: (URL) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var query = ""
    @State private var pendingClearScope: HistoryClearScope?
    @State private var pendingVisitDeletion: HistoryVisit?

    public init(
        model: CompanionAppModel,
        openURL: @escaping (URL) -> Void
    ) {
        self.model = model
        self.openURL = openURL
    }

    public var body: some View {
        NavigationStack {
            Group {
                if sections.isEmpty {
                    ContentUnavailableView.search(text: query)
                } else {
                    List {
                        ForEach(sections) { section in
                            Section(section.title) {
                                ForEach(section.visits) { visit in
                                    historyRow(visit)
                                }
                            }
                        }
                    }
                    .listStyle(.insetGrouped)
                }
            }
            .navigationTitle(CompanionL10n.string(
                "browser.history.title",
                fallback: "History"
            ))
            .searchable(
                text: $query,
                prompt: CompanionL10n.string(
                    "browser.history.search",
                    fallback: "Search history"
                )
            )
            .accessibilityIdentifier("browser.history.search")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string("action.done", fallback: "Done")) {
                        dismiss()
                    }
                    .accessibilityIdentifier("browser.history.done")
                }
                ToolbarItem(placement: .primaryAction) {
                    Menu {
                        ForEach(HistoryClearScope.allCases) { scope in
                            Button(role: .destructive) {
                                pendingClearScope = scope
                            } label: {
                                Label(scope.title, systemImage: "trash")
                            }
                            .accessibilityIdentifier(
                                "browser.history.clear.\(scope.rawValue)"
                            )
                        }
                    } label: {
                        Image(systemName: "trash")
                    }
                    .disabled(model.snapshot.visibleHistory.isEmpty)
                    .accessibilityLabel(CompanionL10n.string(
                        "browser.history.clear",
                        fallback: "Clear history"
                    ))
                    .accessibilityIdentifier("browser.history.clear")
                }
            }
        }
        .accessibilityIdentifier("browser.history.sheet")
        .confirmationDialog(
            pendingClearScope?.confirmationTitle ?? "",
            isPresented: Binding(
                get: { pendingClearScope != nil },
                set: { if !$0 { pendingClearScope = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string("action.delete", fallback: "Delete"),
                role: .destructive
            ) {
                guard let scope = pendingClearScope else { return }
                pendingClearScope = nil
                Task {
                    await model.deleteHistory(
                        sinceMilliseconds: scope.cutoffMilliseconds
                    )
                }
            }
            .accessibilityIdentifier("browser.history.clear.confirm")
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                pendingClearScope = nil
            }
            .accessibilityIdentifier("browser.history.clear.cancel")
        } message: {
            Text(CompanionL10n.string(
                "browser.history.clear.message",
                fallback: "Deleted visits are removed from Ahoi sync on your other devices too."
            ))
        }
        .accessibilityAction(.escape) { dismiss() }
        .confirmationDialog(
            pendingVisitDeletion.map {
                CompanionL10n.format(
                    "browser.history.delete.confirmation",
                    fallback: "Delete %@ from history?",
                    $0.title.isEmpty ? $0.url : $0.title
                )
            } ?? "",
            isPresented: Binding(
                get: { pendingVisitDeletion != nil },
                set: { if !$0 { pendingVisitDeletion = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(
                CompanionL10n.string("action.delete", fallback: "Delete"),
                role: .destructive
            ) {
                guard let visit = pendingVisitDeletion else { return }
                pendingVisitDeletion = nil
                Task { await model.deleteHistoryVisit(visit.id) }
            }
            .accessibilityIdentifier("browser.history.delete.confirm")
            Button(CompanionL10n.string("action.cancel", fallback: "Cancel"), role: .cancel) {
                pendingVisitDeletion = nil
            }
            .accessibilityIdentifier("browser.history.delete.cancel")
        } message: {
            Text(CompanionL10n.string(
                "browser.history.clear.message",
                fallback: "Deleted visits are removed from Ahoi sync on your other devices too."
            ))
        }
    }

    @ViewBuilder
    private func historyRow(_ visit: HistoryVisit) -> some View {
        Button {
            guard let url = URL(string: visit.url) else { return }
            openURL(url)
            dismiss()
        } label: {
            HStack(spacing: 12) {
                Image(systemName: "clock.arrow.circlepath")
                    .foregroundStyle(.secondary)
                    .frame(width: 22)
                VStack(alignment: .leading, spacing: 3) {
                    Text(visit.title.isEmpty ? visit.url : visit.title)
                        .foregroundStyle(.primary)
                        .lineLimit(1)
                    Text(visit.url)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                Spacer(minLength: 8)
                Text(visitDate(visit), format: .dateTime.hour().minute())
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(
            "browser.history.row.\(visit.id.rawValue.uuidString.lowercased())"
        )
        .swipeActions {
            Button(role: .destructive) {
                pendingVisitDeletion = visit
            } label: {
                Label(
                    CompanionL10n.string("action.delete", fallback: "Delete"),
                    systemImage: "trash"
                )
            }
            .accessibilityIdentifier(
                "browser.history.delete.\(visit.id.rawValue.uuidString.lowercased())"
            )
        }
        .accessibilityHint(CompanionL10n.string(
            "browser.history.open_hint",
            fallback: "Opens this page in the current tab"
        ))
    }

    private var filteredVisits: [HistoryVisit] {
        let value = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty else { return model.snapshot.visibleHistory }
        return model.snapshot.visibleHistory.filter {
            $0.title.localizedCaseInsensitiveContains(value) ||
                $0.url.localizedCaseInsensitiveContains(value)
        }
    }

    private var sections: [HistoryDaySection] {
        let calendar = Calendar.autoupdatingCurrent
        let grouped = Dictionary(grouping: filteredVisits) {
            calendar.startOfDay(for: visitDate($0))
        }
        return grouped.keys.sorted(by: >).map { day in
            HistoryDaySection(
                day: day,
                visits: grouped[day, default: []].sorted {
                    $0.visitedAt > $1.visitedAt
                }
            )
        }
    }

    private func visitDate(_ visit: HistoryVisit) -> Date {
        Date(timeIntervalSince1970: Double(
            visit.visitedAt.physicalMilliseconds
        ) / 1_000)
    }
}

private struct HistoryDaySection: Identifiable {
    let day: Date
    let visits: [HistoryVisit]

    var id: Date { day }

    var title: String {
        if Calendar.autoupdatingCurrent.isDateInToday(day) {
            return CompanionL10n.string("browser.history.today", fallback: "Today")
        }
        if Calendar.autoupdatingCurrent.isDateInYesterday(day) {
            return CompanionL10n.string("browser.history.yesterday", fallback: "Yesterday")
        }
        return day.formatted(date: .abbreviated, time: .omitted)
    }
}

private enum HistoryClearScope: String, CaseIterable, Identifiable {
    case lastHour
    case today
    case all

    var id: String { rawValue }

    var title: String {
        switch self {
        case .lastHour:
            CompanionL10n.string("browser.history.clear.hour", fallback: "Last hour")
        case .today:
            CompanionL10n.string("browser.history.clear.today", fallback: "Today")
        case .all:
            CompanionL10n.string("browser.history.clear.all", fallback: "All history")
        }
    }

    var confirmationTitle: String {
        CompanionL10n.format(
            "browser.history.clear.confirmation",
            fallback: "Delete %@?",
            title
        )
    }

    var cutoffMilliseconds: UInt64 {
        let cutoff: Date
        switch self {
        case .lastHour:
            cutoff = Date().addingTimeInterval(-60 * 60)
        case .today:
            cutoff = Calendar.autoupdatingCurrent.startOfDay(for: Date())
        case .all:
            return 0
        }
        return UInt64(max(0, cutoff.timeIntervalSince1970 * 1_000))
    }
}
