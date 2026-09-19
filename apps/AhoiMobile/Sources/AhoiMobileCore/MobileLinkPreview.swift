import SwiftUI
import WebKit

@MainActor
final class MobileLinkPreviewSession: ObservableObject, Identifiable {
    let id: UUID
    let link: MobilePendingLink
    let page: WebPage
    let dialogPresenter: MobileWebDialogPresenter
    @Published var failure: MobilePageFailureKind?

    init(
        id: UUID,
        link: MobilePendingLink,
        page: WebPage,
        dialogPresenter: MobileWebDialogPresenter
    ) {
        self.id = id
        self.link = link
        self.page = page
        self.dialogPresenter = dialogPresenter
    }
}

extension MobileBrowserController {
    @discardableResult
    func stagePendingLinkPreview(requestID: UUID) -> Bool {
        guard let link = pendingLink,
              link.id == requestID,
              selectedTabID == link.sourceTabID,
              let sourceTab = tabs.first(where: { $0.id == link.sourceTabID }),
              sourceTab.mode == link.sourceMode,
              sourceTab.workspaceID == link.workspaceID else {
            return false
        }
        stagedLinkPreview = link
        return true
    }

    func presentStagedLinkPreview() {
        guard let link = stagedLinkPreview else { return }
        stagedLinkPreview = nil
        guard selectedTabID == link.sourceTabID,
              let sourceTab = tabs.first(where: { $0.id == link.sourceTabID }),
              sourceTab.mode == link.sourceMode,
              sourceTab.workspaceID == link.workspaceID else { return }
        dismissLinkPreview()
        let previewID = UUID()
        let page = makePage(tabID: previewID, mode: link.sourceMode)
        guard let presenter = dialogPresenters[previewID] else {
            discardLinkPreviewResources(id: previewID, page: page)
            return
        }
        linkPreview = MobileLinkPreviewSession(
            id: previewID,
            link: link,
            page: page,
            dialogPresenter: presenter
        )
        pendingLink = nil
        observeLinkPreviewNavigation(id: previewID, page: page)
        page.load(link.url)
    }

    func dismissLinkPreview(id: UUID? = nil) {
        guard let preview = linkPreview,
              id == nil || preview.id == id else { return }
        linkPreview = nil
        discardLinkPreviewResources(id: preview.id, page: preview.page)
    }

    @discardableResult
    func adoptLinkPreview(id: UUID) -> UUID? {
        guard let preview = linkPreview,
              preview.id == id,
              let sourceTab = tabs.first(where: { $0.id == preview.link.sourceTabID }),
              sourceTab.mode == preview.link.sourceMode,
              sourceTab.workspaceID == preview.link.workspaceID else {
            dismissLinkPreview(id: id)
            return nil
        }
        let candidateURL = preview.page.url ?? preview.link.url
        guard let safeURL = try? MobileBrowserInputRouter.validateWebURL(candidateURL) else {
            dismissLinkPreview(id: id)
            return nil
        }

        let record = MobileTabRecord(
            id: preview.id,
            workspaceID: preview.link.workspaceID,
            title: preview.page.title,
            url: safeURL.absoluteString,
            mode: preview.link.sourceMode
        )
        linkPreview = nil
        linkPreviewNavigationTask?.cancel()
        linkPreviewNavigationTask = nil
        tabs.append(record)
        pages[record.id] = preview.page
        observeNavigations(of: preview.page, tabID: record.id)
        selectedTabID = record.id
        discardInactivePages(keeping: 5)
        recordTabState()
        persistSoon()
        return record.id
    }

    func retryLinkPreview(id: UUID) {
        guard let preview = linkPreview, preview.id == id else { return }
        preview.failure = nil
        observeLinkPreviewNavigation(id: id, page: preview.page)
        preview.page.load(preview.page.url ?? preview.link.url)
    }

    func cancelStagedLinkPreview(sourceTabID: UUID? = nil) {
        guard sourceTabID == nil || stagedLinkPreview?.sourceTabID == sourceTabID else { return }
        stagedLinkPreview = nil
    }

    private func observeLinkPreviewNavigation(id: UUID, page: WebPage) {
        linkPreviewNavigationTask?.cancel()
        let navigations = page.navigations
        linkPreviewNavigationTask = Task { @MainActor [weak self, weak page] in
            guard let self, let page else { return }
            do {
                for try await event in navigations {
                    guard !Task.isCancelled,
                          self.linkPreview?.id == id,
                          self.linkPreview?.page === page else { return }
                    switch event {
                    case .startedProvisionalNavigation, .committed, .finished,
                         .receivedServerRedirect:
                        self.linkPreview?.failure = nil
                    @unknown default:
                        break
                    }
                }
            } catch {
                guard !Task.isCancelled,
                      self.linkPreview?.id == id,
                      self.linkPreview?.page === page else { return }
                if Self.isNavigationCancellation(error) {
                    Task { @MainActor [weak self, weak page] in
                        await Task.yield()
                        guard let self, let page,
                              self.linkPreview?.id == id,
                              self.linkPreview?.page === page else { return }
                        self.observeLinkPreviewNavigation(id: id, page: page)
                    }
                } else {
                    self.linkPreview?.failure = Self.classifyNavigationFailure(error)
                }
            }
        }
    }

    private func discardLinkPreviewResources(id: UUID, page: WebPage) {
        linkPreviewNavigationTask?.cancel()
        linkPreviewNavigationTask = nil
        page.stopLoading()
        permissionCoordinator.cancelPending(forTabID: id)
        dialogPresenters.removeValue(forKey: id)?.cancelPending()
        linkInteractionCoordinators.removeValue(forKey: id)?.invalidate()
        websiteDataStores.removeValue(forKey: id)
        navigationObservationTasks.removeValue(forKey: id)?.cancel()
        pageFailures.removeValue(forKey: id)
        expectedPolicyCancellationTabIDs.remove(id)
        faviconFetchInFlight.removeValue(forKey: id)
        faviconAttemptedDocumentURLs.removeValue(forKey: id)
        navigationDocumentGenerations.removeValue(forKey: id)
        clearSharedNavigation(for: id)
    }
}

struct MobileLinkPreviewView: View {
    @ObservedObject var preview: MobileLinkPreviewSession
    @ObservedObject var browser: MobileBrowserController
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if let failure = preview.failure {
                    MobilePageFailureView(failure: failure) {
                        browser.retryLinkPreview(id: preview.id)
                    }
                } else {
                    WebView(preview.page)
                        .webViewLinkPreviews(.disabled)
                        .webViewBackForwardNavigationGestures(.enabled)
                        .webViewTextSelection(.enabled)
                        .webViewMagnificationGestures(.enabled)
                        .webViewElementFullscreenBehavior(.enabled)
                }
            }
                .overlay {
                    MobileWebDialogHost(
                        presenter: preview.dialogPresenter,
                        onPresentationRequested: {}
                    )
                }
                .safeAreaInset(edge: .top) {
                    originBanner
                }
                .navigationTitle(CompanionL10n.string(
                    "browser.link_preview.title",
                    fallback: "Link Preview"
                ))
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button {
                            browser.dismissLinkPreview(id: preview.id)
                            dismiss()
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.link_preview.back",
                                    fallback: "Back to Page"
                                ),
                                systemImage: "chevron.backward"
                            )
                        }
                        .accessibilityIdentifier("browser.link-preview.back")
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button {
                            _ = browser.adoptLinkPreview(id: preview.id)
                            dismiss()
                        } label: {
                            Label(
                                CompanionL10n.string(
                                    "browser.link_preview.open_tab",
                                    fallback: "Open as Tab"
                                ),
                                systemImage: "plus.square.on.square"
                            )
                        }
                        .accessibilityIdentifier("browser.link-preview.open-tab")
                        .disabled(preview.failure != nil)
                    }
                }
        }
        .accessibilityIdentifier("browser.link-preview")
        .onDisappear {
            browser.dismissLinkPreview(id: preview.id)
        }
    }

    private var originBanner: some View {
        VStack(alignment: .leading, spacing: 4) {
            Label(destinationOrigin, systemImage: securitySymbol)
                .font(.subheadline.weight(.semibold))
                .lineLimit(1)
            Text(CompanionL10n.format(
                "browser.link_preview.from",
                fallback: "Opened from %@",
                preview.link.sourceOrigin
            ))
            .font(.caption)
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.regularMaterial)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("browser.link-preview.origin")
    }

    private var destinationOrigin: String {
        MobileBrowserOriginFormatter.label(for: preview.page.url ?? preview.link.url)
    }

    private var securitySymbol: String {
        (preview.page.url ?? preview.link.url).scheme?.lowercased() == "https"
            ? "lock.fill"
            : "exclamationmark.triangle"
    }
}
