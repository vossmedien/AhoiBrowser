import Foundation
import WebKit

/// ADR 0012 section 4, step 1 (MOB-EXT-01): a spike that loads one bundled
/// MV3 test extension into a `WKWebExtensionController` and attaches it to
/// normal-browsing pages that use the default website data store. Off unless
/// the app is launched with `-AhoiWebExtensionSpike`. Private pages and
/// separated Workspaces never get it; tab/window adapters
/// (`WKWebExtensionTab`) are out of scope, see
/// `outputs/AhoiBrowser-Mobile-uBlock-Feasibility.md`.
@MainActor
public final class MobileWebExtensionRuntime {
    public static let launchArgument = "-AhoiWebExtensionSpike"
    public static let shared = MobileWebExtensionRuntime(
        arguments: ProcessInfo.processInfo.arguments
    )

    public enum State: Equatable, Sendable {
        case disabled
        case loading
        case loaded(displayName: String)
        case failed(String)
    }

    public private(set) var state: State
    let controller: WKWebExtensionController?
    private var loadTask: Task<Void, Never>?

    init(arguments: [String]) {
        if arguments.contains(Self.launchArgument) {
            // Extension storage stays in memory for the spike: nothing
            // survives a relaunch, and nothing leaves the device.
            controller = WKWebExtensionController(configuration: .nonPersistent())
            state = .loading
        } else {
            controller = nil
            state = .disabled
        }
    }

    /// The unpacked test extension inside the core bundle.
    public static var spikeExtensionURL: URL? {
        resourceBundle.url(forResource: "WebExtensionSpike", withExtension: nil)
    }

    /// Call before `WebPage(configuration:)`. Loading is asynchronous, so
    /// the first page may open before the extension is ready; content scripts
    /// and rules apply from the next navigation on.
    public func attach(
        to configuration: inout WebPage.Configuration,
        mode: MobileBrowsingMode,
        usesDefaultWebsiteDataStore: Bool
    ) {
        guard let controller,
              mode != .privateBrowsing,
              usesDefaultWebsiteDataStore else {
            return
        }
        configuration.webExtensionController = controller
        loadIfNeeded(into: controller)
    }

    /// Waits for the spike extension; for tests and diagnostics.
    public func waitUntilLoaded() async {
        await loadTask?.value
    }

    private func loadIfNeeded(into controller: WKWebExtensionController) {
        guard loadTask == nil else { return }
        loadTask = Task { [weak self] in
            do {
                guard let url = Self.spikeExtensionURL else {
                    throw CocoaError(.fileNoSuchFile)
                }
                let webExtension = try await WKWebExtension(resourceBaseURL: url)
                let context = WKWebExtensionContext(for: webExtension)
                // Debug-only spike: grant what the manifest asks for. The v1
                // scope (MOB-EXT-03) replaces this with a per-site prompt.
                for permission in webExtension.requestedPermissions {
                    context.setPermissionStatus(.grantedExplicitly, for: permission)
                }
                for pattern in webExtension.allRequestedMatchPatterns {
                    context.setPermissionStatus(.grantedExplicitly, for: pattern)
                }
                try controller.load(context)
                self?.state = .loaded(displayName: webExtension.displayName ?? "")
            } catch {
                self?.state = .failed(String(describing: error))
            }
        }
    }

    private final class BundleMarker {}

    private static var resourceBundle: Bundle {
        #if SWIFT_PACKAGE
        Bundle.module
        #else
        Bundle(for: BundleMarker.self)
        #endif
    }
}
