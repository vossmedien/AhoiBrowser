import Foundation
import Combine
import SwiftUI
import WebKit

struct MobileWebPageView: View {
    @Environment(\.accessibilityReduceMotion) private var systemReduceMotion
    @Environment(\.mobileBrowserReduceMotionOverride) private var reduceMotionOverride
    let page: WebPage
    let scrollCoordinator: MobileLinkInteractionCoordinator
    @Binding var findNavigatorPresented: Bool
    @Binding var chromeCollapsed: Bool
    let chromeResetGeneration: UInt64
    let onRefresh: () -> Void
    let onMetadataChange: () -> Void
    @State private var findQuery = ""
    @State private var findMatchCount = 0
    @State private var pullDistance: CGFloat = 0
    @State private var refreshArmed = false
    @State private var chromeScrollReducer = MobileChromeScrollReducer()
    @State private var findRequestGeneration: UInt64 = 0
    @State private var findTask: Task<Void, Never>?
    @FocusState private var findFieldFocused: Bool

    private var reduceMotion: Bool {
        reduceMotionOverride ?? systemReduceMotion
    }

    var body: some View {
        ZStack(alignment: .top) {
            WebView(page)
                // iOS does not expose WebKit for SwiftUI's context-menu hook.
                // Ahoi owns link long-press actions through its isolated
                // user-script bridge, so the native preview must not race it.
                .webViewLinkPreviews(.disabled)
                .webViewBackForwardNavigationGestures(.enabled)
                .webViewTextSelection(.enabled)
                .webViewMagnificationGestures(.enabled)
                .webViewElementFullscreenBehavior(.enabled)
                .webViewOnScrollGeometryChange(
                    for: MobileWebScrollState.self,
                    of: { geometry in
                        let topInset = geometry.contentInsets.top
                        let bottomInset = geometry.contentInsets.bottom
                        let rawOffset = geometry.contentOffset.y + topInset
                        return MobileWebScrollState(
                            pullDistance: max(0, -rawOffset),
                            contentWidth: geometry.contentSize.width,
                            contentHeight: geometry.contentSize.height,
                            containerWidth: geometry.containerSize.width,
                            containerHeight: geometry.containerSize.height,
                            topInset: topInset,
                            bottomInset: bottomInset
                        )
                    },
                    action: handleScroll
                )

            if pullDistance > 4 {
                ProgressView()
                    .controlSize(.small)
                    .padding(9)
                    .background(.regularMaterial, in: Circle())
                    .scaleEffect(min(1, 0.55 + pullDistance / 120))
                    .opacity(min(1, pullDistance / 36))
                    .padding(.top, 8)
                    .accessibilityLabel(CompanionL10n.string(
                        "browser.pull_to_refresh",
                        fallback: "Pull to refresh"
                    ))
            }

            if findNavigatorPresented {
                findNavigator
            }
        }
        .onAppear {
            chromeScrollReducer.reset(baseline: scrollCoordinator.latestScrollEvent)
        }
        .onReceive(scrollCoordinator.scrollEvents) { event in
            handlePageScroll(event)
        }
        .onChange(of: page.url) { _, _ in
            resetChromeVisibility(clearScrollBaseline: true)
            findRequestGeneration &+= 1
            findTask?.cancel()
            findTask = nil
            findMatchCount = 0
            onMetadataChange()
        }
        .onChange(of: page.title) { _, _ in onMetadataChange() }
        .onChange(of: page.isLoading) { _, isLoading in
            if isLoading {
                resetChromeVisibility(clearScrollBaseline: true)
            } else {
                onMetadataChange()
            }
        }
        .onChange(of: findNavigatorPresented) { _, isPresented in
            if isPresented { resetChromeVisibility() }
        }
        .onChange(of: chromeResetGeneration) { _, _ in
            chromeScrollReducer.reset(baseline: scrollCoordinator.latestScrollEvent)
        }
        .task(id: page.url) {
            try? await Task.sleep(for: .milliseconds(250))
            guard !Task.isCancelled else { return }
            onMetadataChange()
        }
        .onDisappear {
            resetChromeVisibility(clearScrollBaseline: true)
            findRequestGeneration &+= 1
            findTask?.cancel()
            findTask = nil
        }
    }

    private var findNavigator: some View {
        HStack(spacing: 10) {
            TextField(
                CompanionL10n.string("browser.find", fallback: "Find on Page"),
                text: $findQuery
            )
            .textFieldStyle(.roundedBorder)
            .focused($findFieldFocused)
            .submitLabel(.search)
            .onSubmit { performFind(backwards: false, resetSelection: false) }
            .accessibilityIdentifier("browser.find.field")

            Text(CompanionL10n.format(
                "browser.find.results",
                fallback: "%d matches",
                findMatchCount
            ))
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)
            .lineLimit(1)

            Button { performFind(backwards: true, resetSelection: false) } label: {
                Image(systemName: "chevron.up")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .disabled(findQuery.isEmpty)
            .accessibilityLabel(CompanionL10n.string(
                "browser.find.previous",
                fallback: "Previous match"
            ))

            Button { performFind(backwards: false, resetSelection: false) } label: {
                Image(systemName: "chevron.down")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .disabled(findQuery.isEmpty)
            .accessibilityLabel(CompanionL10n.string(
                "browser.find.next",
                fallback: "Next match"
            ))

            Button(action: closeFindNavigator) {
                Image(systemName: "xmark.circle.fill")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .accessibilityLabel(CompanionL10n.string(
                "action.close",
                fallback: "Close"
            ))
        }
        .padding(10)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .padding(.horizontal, 10)
        .padding(.top, 8)
        .shadow(radius: 8, y: 3)
        .accessibilityIdentifier("browser.find.navigator")
        .onAppear { findFieldFocused = true }
        .onChange(of: findQuery) { _, _ in
            performFind(backwards: false, resetSelection: true)
        }
    }

    private func closeFindNavigator() {
        findRequestGeneration &+= 1
        findTask?.cancel()
        findTask = nil
        findNavigatorPresented = false
        findQuery = ""
        findMatchCount = 0
        clearFindSelection()
    }

    private func handlePullDistance(_ oldValue: CGFloat, _ newValue: CGFloat) {
        pullDistance = newValue
        if newValue >= 72 {
            refreshArmed = true
        } else if refreshArmed && oldValue > 8 && newValue <= 8 {
            refreshArmed = false
            onRefresh()
        }
    }

    private func handleScroll(
        _ oldValue: MobileWebScrollState,
        _ newValue: MobileWebScrollState
    ) {
        handlePullDistance(oldValue.pullDistance, newValue.pullDistance)
        guard newValue.pullDistance <= 0.5 else {
            chromeScrollReducer.invalidateBaseline()
            reportChromeCollapsed(false)
            return
        }
        if !newValue.hasStableLayout(comparedTo: oldValue) {
            chromeScrollReducer.invalidateBaseline()
        }
    }

    private func handlePageScroll(_ event: MobilePageScrollEvent) {
        let collapsed = chromeScrollReducer.nextCollapsedState(
            event: event,
            currentlyCollapsed: chromeCollapsed,
            pullDistance: pullDistance,
            suspended: findNavigatorPresented
        )
        reportChromeCollapsed(collapsed)
    }

    private func reportChromeCollapsed(_ collapsed: Bool) {
        guard chromeCollapsed != collapsed else { return }
        withAnimation(MobileBrowserChromeTheme.chromeAnimation(
            toCollapsed: collapsed,
            reduceMotion: reduceMotion
        )) {
            chromeCollapsed = collapsed
        }
    }

    private func resetChromeVisibility(clearScrollBaseline: Bool = false) {
        chromeScrollReducer.reset(baseline: clearScrollBaseline
                                  ? nil
                                  : scrollCoordinator.latestScrollEvent)
        reportChromeCollapsed(false)
    }

    private func performFind(backwards: Bool, resetSelection: Bool) {
        let query = findQuery.trimmingCharacters(in: .whitespacesAndNewlines)
        findRequestGeneration &+= 1
        let requestGeneration = findRequestGeneration
        findTask?.cancel()
        guard !query.isEmpty else {
            findTask = nil
            findMatchCount = 0
            clearFindSelection()
            return
        }
        findTask = Task { @MainActor in
            defer {
                if findRequestGeneration == requestGeneration {
                    findTask = nil
                }
            }
            do {
                let result = try await page.callJavaScript(
                    """
                    const needle = query.toLocaleLowerCase();
                    if (resetSelection) window.getSelection()?.removeAllRanges();
                    window.find(query, false, backwards, true, false, false, false);
                    const haystack = (document.body?.innerText || '').toLocaleLowerCase();
                    return haystack.split(needle).length - 1;
                    """,
                    arguments: [
                        "query": query,
                        "backwards": backwards,
                        "resetSelection": resetSelection,
                    ]
                )
                guard !Task.isCancelled,
                      findRequestGeneration == requestGeneration,
                      findQuery.trimmingCharacters(in: .whitespacesAndNewlines) == query,
                      findNavigatorPresented else {
                    return
                }
                if let count = result as? Int {
                    findMatchCount = count
                } else if let count = result as? NSNumber {
                    findMatchCount = count.intValue
                }
            } catch {
                if !Task.isCancelled, findRequestGeneration == requestGeneration {
                    findMatchCount = 0
                }
            }
        }
    }

    private func clearFindSelection() {
        Task { @MainActor in
            _ = try? await page.callJavaScript("window.getSelection()?.removeAllRanges();")
        }
    }
}

private struct MobileWebScrollState: Hashable {
    let pullDistance: CGFloat
    let contentWidth: CGFloat
    let contentHeight: CGFloat
    let containerWidth: CGFloat
    let containerHeight: CGFloat
    let topInset: CGFloat
    let bottomInset: CGFloat

    func hasStableLayout(comparedTo other: Self) -> Bool {
        abs(contentWidth - other.contentWidth) < 0.5 &&
            abs(contentHeight - other.contentHeight) < 0.5 &&
            abs(containerWidth - other.containerWidth) < 0.5 &&
            abs(containerHeight - other.containerHeight) < 0.5 &&
            abs(topInset - other.topInset) < 0.5 &&
            abs(bottomInset - other.bottomInset) < 0.5
    }
}
