import Foundation
import SwiftUI
import AhoiCloudKitSpike

@MainActor
struct MobileSharedTabIntentBinding: ViewModifier {
    let browser: MobileBrowserController
    let model: CompanionAppModel
    let enabled: Bool

    func body(content: Content) -> some View {
        content.onAppear {
            guard enabled else { return }
            browser.onSharedTabIntent = { [weak browser, weak model] tab, intent in
                guard let browser, let model else { return }
                model.receiveSharedTabIntent(tab, intent, browser: browser)
            }
        }.onDisappear {
            browser.onSharedTabIntent = nil
        }.onChange(of: browser.normalTabs.map(CaptureKey.init)) { _, _ in
            guard enabled else { return }
            model.scheduleMobileSharedCapture(browser)
        }
    }

    // Do not enqueue work for favicon/tint updates or page progress.
    private struct CaptureKey: Equatable {
        let id: UUID
        let presence: TabID?
        let page: TreeNodeID?
        let workspace: WorkspaceID?
        let target: SharedTabTarget?
        let binding: MobileSharedTabBindingState
        let participates: Bool
        let saved: Bool
        let customTitle: String?

        init(_ tab: MobileTabRecord) {
            id = tab.id
            presence = tab.presenceID
            page = tab.treeNodeID
            workspace = tab.workspaceID
            target = tab.sharedTarget
            binding = tab.sharedBindingState
            participates = tab.participatesInSharedTabs
            saved = tab.isSaved
            customTitle = tab.customTitle
        }
    }
}

extension CompanionAppModel {
    func scheduleMobileSharedCapture(_ browser: MobileBrowserController) {
        mobileSharedCaptureRequested = true
        guard mobileSharedCaptureTask == nil else { return }
        mobileSharedCaptureTask = Task { [weak self, weak browser] in
            guard let self else { return }
            defer { self.mobileSharedCaptureTask = nil }
            repeat {
                self.mobileSharedCaptureRequested = false
                guard let browser else { return }
                await self.reconcilePublishedMobileTabs(browser)
            } while self.mobileSharedCaptureRequested
        }
    }
}
