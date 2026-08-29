import SwiftUI
import UIKit
import AhoiCloudKitSpike

struct MobileLinkActionSheet: View {
    let link: MobilePendingLink

    @ObservedObject private var companionModel: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController
    @Environment(\.dismiss) private var dismiss
    @State private var selectedWorkspaceID: WorkspaceID?
    @State private var isSaving = false
    @State private var saveError: String?

    init(
        link: MobilePendingLink,
        companionModel: CompanionAppModel,
        browser: MobileBrowserController
    ) {
        self.link = link
        _companionModel = ObservedObject(wrappedValue: companionModel)
        _browser = ObservedObject(wrappedValue: browser)

        let workspaces = companionModel.snapshot.visibleWorkspaces
        let sourceWorkspace = link.workspaceID.flatMap { sourceID in
            workspaces.first(where: { $0.id == sourceID })?.id
        }
        _selectedWorkspaceID = State(
            initialValue: sourceWorkspace ?? workspaces.first?.id
        )
    }

    var body: some View {
        NavigationStack {
            List {
                linkIdentitySection
                openSection
                workspaceSection
                linkUtilitySection
            }
            .navigationTitle(CompanionL10n.string(
                "browser.link_actions.title",
                fallback: "Link Actions"
            ))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(CompanionL10n.string(
                        "action.cancel",
                        fallback: "Cancel"
                    )) {
                        finish()
                    }
                }
            }
        }
        .presentationDetents([.medium, .large])
        .presentationDragIndicator(.visible)
        .onDisappear {
            browser.dismissPendingLink()
        }
        .alert(
            CompanionL10n.string(
                "browser.link_actions.save_failed.title",
                fallback: "Link Not Saved"
            ),
            isPresented: Binding(
                get: { saveError != nil },
                set: { if !$0 { saveError = nil } }
            )
        ) {
            Button(CompanionL10n.string("action.ok", fallback: "OK")) {
                saveError = nil
            }
        } message: {
            Text(saveError ?? "")
        }
    }

    private var linkIdentitySection: some View {
        Section {
            LabeledContent {
                Text(link.url.absoluteString)
                    .font(.callout)
                    .foregroundStyle(.primary)
                    .multilineTextAlignment(.trailing)
                    .lineLimit(3)
                    .textSelection(.enabled)
            } label: {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.url",
                        fallback: "Link"
                    ),
                    systemImage: "link"
                )
            }
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("browser.link-actions.url")

            LabeledContent {
                Text(link.sourceOrigin)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.trailing)
                    .lineLimit(2)
            } label: {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.origin",
                        fallback: "From"
                    ),
                    systemImage: "lock.shield"
                )
            }
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("browser.link-actions.origin")
        }
    }

    private var openSection: some View {
        Section(CompanionL10n.string(
            "browser.link_actions.open_section",
            fallback: "Open"
        )) {
            Button {
                open(mode: .normal)
            } label: {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.open_new_tab",
                        fallback: "Open in New Tab"
                    ),
                    systemImage: "plus.square"
                )
            }
            .accessibilityIdentifier("browser.link-actions.open-normal")

            Button {
                open(mode: .privateBrowsing)
            } label: {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.open_private",
                        fallback: "Open Privately"
                    ),
                    systemImage: "hand.raised.fill"
                )
            }
            .accessibilityIdentifier("browser.link-actions.open-private")
        }
    }

    @ViewBuilder
    private var workspaceSection: some View {
        if workspaces.isEmpty {
            Section(CompanionL10n.string(
                "browser.link_actions.workspace",
                fallback: "Workspace"
            )) {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.no_workspaces",
                        fallback: "Create a workspace before saving this link."
                    ),
                    systemImage: "folder.badge.questionmark"
                )
                .foregroundStyle(.secondary)
            }
        } else {
            Section(CompanionL10n.string(
                "browser.link_actions.workspace",
                fallback: "Workspace"
            )) {
                Picker(
                    CompanionL10n.string(
                        "browser.link_actions.workspace_picker",
                        fallback: "Selected Workspace"
                    ),
                    selection: $selectedWorkspaceID
                ) {
                    ForEach(workspaces) { workspace in
                        Text(workspace.name)
                            .tag(Optional(workspace.id))
                    }
                }
                .accessibilityIdentifier("browser.link-actions.workspace-picker")

                Button {
                    open(
                        mode: .normal,
                        workspaceID: selectedWorkspaceID
                    )
                } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.link_actions.open_workspace",
                            fallback: "Open in Selected Workspace"
                        ),
                        systemImage: "folder"
                    )
                }
                .disabled(selectedWorkspaceID == nil)
                .accessibilityIdentifier("browser.link-actions.open-workspace")

                Button {
                    saveToSelectedWorkspace()
                } label: {
                    Label(
                        CompanionL10n.string(
                            "browser.link_actions.save",
                            fallback: "Save Link"
                        ),
                        systemImage: "bookmark"
                    )
                }
                .disabled(selectedWorkspaceID == nil || isSaving)
                .accessibilityIdentifier("browser.link-actions.save")
            }
        }
    }

    private var linkUtilitySection: some View {
        Section(CompanionL10n.string(
            "browser.link_actions.use_section",
            fallback: "Use Link"
        )) {
            Button {
                UIPasteboard.general.url = link.url
                finish()
            } label: {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.copy",
                        fallback: "Copy Link"
                    ),
                    systemImage: "doc.on.doc"
                )
            }
            .accessibilityIdentifier("browser.link-actions.copy")

            ShareLink(
                item: link.url,
                preview: SharePreview(link.url.host() ?? link.url.absoluteString)
            ) {
                Label(
                    CompanionL10n.string(
                        "browser.link_actions.share",
                        fallback: "Share Link"
                    ),
                    systemImage: "square.and.arrow.up"
                )
            }
            .accessibilityIdentifier("browser.link-actions.share")
        }
    }

    private var workspaces: [Workspace] {
        companionModel.snapshot.visibleWorkspaces
    }

    private func open(
        mode: MobileBrowsingMode,
        workspaceID: WorkspaceID? = nil
    ) {
        _ = browser.openPendingLink(
            mode: mode,
            workspaceID: workspaceID
        )
        dismiss()
    }

    private func saveToSelectedWorkspace() {
        guard let selectedWorkspaceID else { return }
        isSaving = true
        Task { @MainActor in
            let saved = await companionModel.createSavedPage(
                workspaceID: selectedWorkspaceID,
                title: link.url.host() ?? link.url.absoluteString,
                url: link.url.absoluteString
            )
            isSaving = false
            if saved != nil {
                finish()
            } else {
                saveError = companionModel.loadError ?? CompanionL10n.string(
                    "browser.link_actions.save_failed.message",
                    fallback: "The link could not be saved."
                )
            }
        }
    }

    private func finish() {
        browser.dismissPendingLink()
        dismiss()
    }
}
