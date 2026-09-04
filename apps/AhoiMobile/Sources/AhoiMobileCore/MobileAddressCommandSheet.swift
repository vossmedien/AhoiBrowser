import Foundation
import SwiftUI
import AhoiCloudKitSpike

struct MobileAddressCommandSheet: View {
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass
    private let companionModel: CompanionAppModel
    @ObservedObject private var browser: MobileBrowserController
    @Binding private var isPresented: Bool
    @Binding private var addressText: String
    @Binding private var addressSelection: TextSelection?
    @FocusState private var addressFieldFocused: Bool
    private let searchEngine: MobileSearchEngine

    init(
        companionModel: CompanionAppModel,
        browser: MobileBrowserController,
        isPresented: Binding<Bool>,
        addressText: Binding<String>,
        addressSelection: Binding<TextSelection?>,
        searchEngine: MobileSearchEngine
    ) {
        self.companionModel = companionModel
        _browser = ObservedObject(wrappedValue: browser)
        _isPresented = isPresented
        _addressText = addressText
        _addressSelection = addressSelection
        self.searchEngine = searchEngine
    }

    var body: some View {
        MobileHardwareEscapeContainer(onEscape: dismissAddressEditor) {
            NavigationStack {
                VStack(spacing: 14) {
                    HStack(spacing: 8) {
                        TextField(
                            CompanionL10n.string(
                                "browser.search_or_address",
                                fallback: "Search or enter address"
                            ),
                            text: $addressText,
                            selection: $addressSelection
                        )
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.webSearch)
                        .submitLabel(.go)
                        .onSubmit(commitAddress)
                        .textFieldStyle(.roundedBorder)
                        .font(.title3)
                        .accessibilityIdentifier("browser.address.field")
                        .focused($addressFieldFocused)
                        .task {
                            await focusAddressFieldAfterPresentation()
                        }

                        if !addressText.isEmpty {
                            Button {
                                addressText = ""
                                addressSelection = TextSelection(
                                    insertionPoint: addressText.startIndex
                                )
                                addressFieldFocused = true
                                browser.dismissError()
                            } label: {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundStyle(.secondary)
                            }
                            .buttonStyle(.plain)
                            .accessibilityIdentifier("browser.address.clear")
                            .accessibilityLabel(CompanionL10n.string(
                                "browser.clear_address",
                                fallback: "Clear address"
                            ))
                        }
                    }

                    if let error = browser.lastError {
                        Label(error, systemImage: "exclamationmark.triangle.fill")
                            .font(.callout)
                            .foregroundStyle(.red)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .accessibilityIdentifier("browser.error.message")
                    }

                    MobileAddressCommandResults(
                        companionModel: companionModel,
                        browser: browser,
                        isPresented: $isPresented,
                        addressText: addressText,
                        searchEngine: searchEngine,
                        onCommit: commitAddress,
                        onActivateSearchResult: activateSearchResult
                    )
                }
                .padding()
                .navigationTitle(CompanionL10n.string(
                    "browser.command.title",
                    fallback: "Go to"
                ))
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button(CompanionL10n.string("action.cancel", fallback: "Cancel")) {
                            dismissAddressEditor()
                        }
                    }
                }
                .task(id: addressText) {
                    let query = addressText
                    try? await Task.sleep(for: .milliseconds(120))
                    guard !Task.isCancelled else { return }
                    await companionModel.refreshSearch(query: query)
                }
                .frame(
                    minWidth: horizontalSizeClass == .regular ? 620 : nil,
                    minHeight: horizontalSizeClass == .regular ? 560 : nil
                )
            }
        }
        .presentationDetents([.medium, .large])
        .modifier(MobileAddressPresentationSizing(isRegularWidth: horizontalSizeClass == .regular))
        .accessibilityAction(.escape) { dismissAddressEditor() }
    }

    private func focusAddressFieldAfterPresentation() async {
        await Task.yield()
        guard !Task.isCancelled, isPresented else { return }
        addressFieldFocused = true
        await Task.yield()
        guard !Task.isCancelled, isPresented else { return }
        selectAllAddressText()
    }

    private func dismissAddressEditor() {
        addressFieldFocused = false
        isPresented = false
    }

    private func selectAllAddressText() {
        addressSelection = TextSelection(range: addressText.startIndex..<addressText.endIndex)
    }

    private func commitAddress() {
        browser.navigate(
            addressText,
            searchTemplate: searchEngine.searchTemplate
        )
        if browser.lastError == nil { dismissAddressEditor() }
    }

    private func activateSearchResult(_ result: CompanionSearchResult) {
        if let value = result.url {
            addressText = value
            commitAddress()
            return
        }
        let workspace: Workspace?
        switch result.kind {
        case .workspace:
            workspace = companionModel.snapshot.visibleWorkspaces.first {
                $0.id.rawValue == result.id
            }
        case .folder:
            let node = companionModel.snapshot.visibleTreeNodes.first {
                $0.id.rawValue == result.id
            }
            workspace = node.flatMap { node in
                companionModel.snapshot.visibleWorkspaces.first {
                    $0.id == node.workspaceID
                }
            }
        case .savedPage, .remoteTab, .history:
            workspace = nil
        }
        guard let workspace else { return }
        if let tab = browser.normalTabs
            .filter({ $0.workspaceID == workspace.id })
            .max(by: { $0.lastActiveAt < $1.lastActiveAt }) {
            browser.select(tab.id)
        } else {
            _ = browser.createTab(workspaceID: workspace.id)
        }
        dismissAddressEditor()
    }
}

/// Search results deliberately observe the companion model below the address
/// field. Publishing an async result therefore updates only this subtree and
/// cannot re-apply a stale `TextSelection` to the actively edited field.
private struct MobileAddressCommandResults: View {
    @ObservedObject var companionModel: CompanionAppModel
    @ObservedObject var browser: MobileBrowserController
    @Binding var isPresented: Bool
    let addressText: String
    let searchEngine: MobileSearchEngine
    let onCommit: () -> Void
    let onActivateSearchResult: (CompanionSearchResult) -> Void

    var body: some View {
        let trimmedInput = addressText.trimmingCharacters(in: .whitespacesAndNewlines)
        let openTabs = browser.searchOpenTabs(addressText)

        if !trimmedInput.isEmpty || !openTabs.isEmpty || !companionModel.searchResults.isEmpty {
            List {
                if !trimmedInput.isEmpty {
                    Section {
                        Button(action: onCommit) {
                            Label {
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(CompanionL10n.format(
                                        "browser.search.navigate",
                                        fallback: "Open or search for ‘%@’",
                                        trimmedInput
                                    ))
                                    .lineLimit(2)
                                    Text(searchEngine.localizedName)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            } icon: {
                                Image(systemName: "arrow.right.circle.fill")
                                    .foregroundStyle(.tint)
                            }
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("browser.search.navigate")
                    }
                }
                if !openTabs.isEmpty {
                    Section(CompanionL10n.string("browser.search.open_tabs", fallback: "Open Tabs")) {
                        ForEach(openTabs.prefix(6)) { tab in
                            Button {
                                browser.select(tab.id)
                                isPresented = false
                            } label: {
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(tab.displayTitle).lineLimit(1)
                                    Text(tab.url ?? "")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                }
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                if !companionModel.searchResults.isEmpty {
                    Section(CompanionL10n.string("browser.search.library", fallback: "Ahoi Library")) {
                        ForEach(companionModel.searchResults.prefix(8)) { result in
                            Button {
                                onActivateSearchResult(result)
                            } label: {
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(result.title).lineLimit(1)
                                    if !result.detail.isEmpty {
                                        Text(result.detail)
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                            .lineLimit(1)
                                    }
                                }
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                Section(CompanionL10n.string(
                    "browser.search.actions",
                    fallback: "Actions"
                )) {
                    Button {
                        _ = browser.createTab()
                        isPresented = false
                    } label: {
                        Label(
                            CompanionL10n.string(
                                "browser.new_tab",
                                fallback: "New tab"
                            ),
                            systemImage: "plus"
                        )
                    }
                    Button {
                        _ = browser.createTab(mode: .privateBrowsing)
                        isPresented = false
                    } label: {
                        Label(
                            CompanionL10n.string(
                                "browser.new_private_tab",
                                fallback: "New private tab"
                            ),
                            systemImage: "hand.raised"
                        )
                    }
                }
            }
            .listStyle(.plain)
        } else {
            ContentUnavailableView(
                CompanionL10n.string("browser.command.empty", fallback: "Ready to sail"),
                systemImage: "magnifyingglass",
                description: Text(CompanionL10n.string(
                    "browser.command.empty.description",
                    fallback: "Enter a website, search, workspace, tab or history item."
                ))
            )
        }
    }
}

private struct MobileAddressPresentationSizing: ViewModifier {
    let isRegularWidth: Bool

    @ViewBuilder
    func body(content: Content) -> some View {
        if isRegularWidth {
            content.presentationSizing(.page)
        } else {
            content
        }
    }
}
