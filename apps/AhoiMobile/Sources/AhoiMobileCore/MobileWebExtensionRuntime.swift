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
#if DEBUG
    public enum FilesSpikeError: Error, Equatable {
        case disabled, alreadyLoaded, notUnpackedFolder, fixtureMismatch, invalidExtension
    }

    /// One user-selected, unpacked copy of the bundled test fixture. This is
    /// deliberately not the general Files installer proposed for v1.
    public private(set) var importedDisplayName: String?
    private var importedContext: WKWebExtensionContext?
    private var importedStagingURL: URL?
    // Main-actor reentrancy: a second pick must not pass the nil check while
    // the first import is suspended, or one context and its copy would leak.
    private var filesImportInProgress = false
    private static let knownFixtureFiles = [
        "manifest.json", "content.js", "rules.json", "popup.html", "popup.js",
    ]
#endif

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

#if DEBUG
    /// Stage only the exact known fixture bytes from a Files-selected folder.
    /// Keeping this behind DEBUG and the explicit spike launch argument lets
    /// Step 1 test Files/WebKit plumbing without enabling arbitrary code in
    /// an App Store candidate or making a Step-2 product decision.
    public func loadUnpackedFromFiles(_ sourceURL: URL) async throws {
        guard let controller else { throw FilesSpikeError.disabled }
        guard importedContext == nil, !filesImportInProgress else {
            throw FilesSpikeError.alreadyLoaded
        }
        filesImportInProgress = true
        defer { filesImportInProgress = false }
        guard sourceURL.isFileURL,
              try sourceURL.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else {
            throw FilesSpikeError.notUnpackedFolder
        }
        guard let bundled = Self.spikeExtensionURL else { throw CocoaError(.fileNoSuchFile) }

        let hasScope = sourceURL.startAccessingSecurityScopedResource()
        defer { if hasScope { sourceURL.stopAccessingSecurityScopedResource() } }
        let staged = FileManager.default.temporaryDirectory.appendingPathComponent(
            "Ahoi-WebExtension-Spike-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: false)
        var loaded = false
        defer { if !loaded { try? FileManager.default.removeItem(at: staged) } }
        for filename in Self.knownFixtureFiles {
            let expected = try Data(contentsOf: bundled.appendingPathComponent(filename))
            let selected = try Data(contentsOf: sourceURL.appendingPathComponent(filename))
            guard selected == expected else { throw FilesSpikeError.fixtureMismatch }
            try selected.write(to: staged.appendingPathComponent(filename),
                               options: [.withoutOverwriting])
        }

        loadIfNeeded(into: controller)
        await loadTask?.value
        guard case .loaded = state else { throw FilesSpikeError.invalidExtension }
        let webExtension = try await WKWebExtension(resourceBaseURL: staged)
        guard webExtension.manifestVersion == 3, webExtension.errors.isEmpty else {
            throw FilesSpikeError.invalidExtension
        }
        let context = WKWebExtensionContext(for: webExtension)
        // The same reviewed test fixture receives the same spike-only grants
        // as the bundled copy. Production permissions still require Step 2.
        for permission in webExtension.requestedPermissions {
            context.setPermissionStatus(.grantedExplicitly, for: permission)
        }
        for pattern in webExtension.allRequestedMatchPatterns {
            context.setPermissionStatus(.grantedExplicitly, for: pattern)
        }
        try controller.load(context)
        importedContext = context
        importedStagingURL = staged
        importedDisplayName = webExtension.displayName ?? ""
        loaded = true
    }

    public func unloadImportedFilesSpike() throws {
        guard let controller else { return }
        if let importedContext {
            try controller.unload(importedContext)
            self.importedContext = nil
            importedDisplayName = nil
        }
        if let importedStagingURL {
            try FileManager.default.removeItem(at: importedStagingURL)
            self.importedStagingURL = nil
        }
    }
#endif

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
