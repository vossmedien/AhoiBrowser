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
    private var permissionExpiryTask: Task<Void, Never>?
    private var permissionChangeInProgress = false
    private var ruleSuspensionRequested = false
    private var ruleSuspensionTask: Task<Void, Never>?
    private var rulePort: WKWebExtension.MessagePort?
    private var rulePortReady = false
    // This tracks an in-flight rule operation, never a copy of grants.
    private var rulesMayBeEnabled = false
    private var ruleRequest: RuleRequest?
    private var ruleTimeoutTask: Task<Void, Never>?
    static let ruleApplicationIdentifier = "ahoi.spike.rules"
    private struct RuleRequest {
        let id: String
        let enabled: Bool
        let isCurrent: @MainActor () -> Bool
        let continuation: CheckedContinuation<Void, any Error>
        var sent = false
        var waiters: [CheckedContinuation<Void, any Error>] = []
    }
    private(set) var context: WKWebExtensionContext?
    lazy var host = MobileWebExtensionHost(runtime: self)
    private var filesImportGeneration = 0
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
#if DEBUG
        if arguments.contains(Self.launchArgument) {
            // Extension storage stays in memory for the spike: nothing
            // survives a relaunch, and nothing leaves the device.
            controller = WKWebExtensionController(configuration: .nonPersistent())
            state = .loading
        } else {
            controller = nil
            state = .disabled
        }
#else
        controller = nil
        state = .disabled
#endif
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
#if DEBUG
        reconcileExpiredRules()
        if let store = controller.configuration.defaultWebsiteDataStore {
            NSLog("Ahoi Spike SDK27 defaultStore persistent=%d identicalToPageStore=%d identity=%@ pageIdentity=%@",
              store.isPersistent, store === configuration.websiteDataStore,
              String(describing: ObjectIdentifier(store)),
              String(describing: ObjectIdentifier(configuration.websiteDataStore)))
        } else {
            NSLog("Ahoi Spike SDK27 defaultStore=nil")
        }
#endif
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
        let generation = filesImportGeneration
        guard sourceURL.isFileURL else {
            throw FilesSpikeError.notUnpackedFolder
        }
        // Files-provider access must precede even directory metadata reads.
        let hasScope = sourceURL.startAccessingSecurityScopedResource()
        defer { if hasScope { sourceURL.stopAccessingSecurityScopedResource() } }
        guard try sourceURL.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else {
            throw FilesSpikeError.notUnpackedFolder
        }
        guard let bundled = Self.spikeExtensionURL else { throw CocoaError(.fileNoSuchFile) }
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
        guard generation == filesImportGeneration else { throw CancellationError() }
        guard webExtension.manifestVersion == 3, webExtension.errors.isEmpty else {
            throw FilesSpikeError.invalidExtension
        }
        let context = WKWebExtensionContext(for: webExtension)
        // Files is a separate context: no automatic API or host grants.
        context.hasAccessToPrivateData = false
        try controller.load(context)
        importedContext = context
        importedStagingURL = staged
        importedDisplayName = webExtension.displayName ?? ""
        loaded = true
    }

    public func unloadImportedFilesSpike() throws {
        filesImportGeneration += 1
        guard let controller else { return }
        if let importedContext {
            host.invalidate(context: importedContext)
            try controller.unload(importedContext)
            host.didUnload(context: importedContext)
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
#if DEBUG
                guard let self else { return }
                context.hasAccessToPrivateData = false
                controller.delegate = self.host // WebKit holds its delegate weakly.
                try controller.load(context)
                self.context = context // Keep WebKit's permission/storage authority alive.
                self.state = .loaded(displayName: webExtension.displayName ?? "")
#endif
            } catch {
                self?.state = .failed(String(describing: error))
            }
        }
    }

#if DEBUG
    func unloadBundledSpike() throws {
        guard let context, let controller else { return }
        permissionExpiryTask?.cancel()
        permissionExpiryTask = nil
        ruleSuspensionTask?.cancel()
        ruleSuspensionTask = nil
        ruleSuspensionRequested = false
        disconnectRulePort()
        host.invalidate(context: context)
        if context.isLoaded { try controller.unload(context) }
        host.didUnload(context: context)
        self.context = nil
        state = .disabled
        // The completed loadTask stays set: this explicit unload must not reload
        // implicitly on the next page. Relaunch is the spike's restart boundary.
    }

    enum SiteError: Error { case unsupportedSite, invalidStatus, busy, ruleUpdateFailed }
    enum SiteChoice: Equatable { case allow, deny, revoke }

    static func sitePattern(for url: URL) throws -> WKWebExtension.MatchPattern {
        guard url.scheme == "https", let host = url.host, !host.isEmpty,
              !host.contains("*"), url.user == nil, url.password == nil else {
            throw SiteError.unsupportedSite
        }
        // WebKit host patterns cover all paths and ports, not one document.
        return try WKWebExtension.MatchPattern(scheme: "https", host: host, path: "/*")
    }

    func setSiteChoice(_ choice: SiteChoice, for url: URL, expiration: Date,
                       isCurrent: @escaping @MainActor () -> Bool = { true }) async throws {
        guard let context, context.isLoaded, expiration > Date(), isCurrent() else {
            throw CancellationError()
        }
        guard !permissionChangeInProgress else { throw SiteError.busy }
        let pattern = try Self.sitePattern(for: url)
        guard context.webExtension.allRequestedMatchPatterns.contains(where: { $0.matches(url) }) else {
            throw SiteError.unsupportedSite
        }
        permissionChangeInProgress = true
        defer {
            permissionChangeInProgress = false
            if ruleSuspensionRequested { suspendRuleControl() }
        }
        do {
            // Confirm an in-flight refresh/suspension before changing grants;
            // cancelling its acknowledgement can leave compiled rules behind.
            await ruleSuspensionTask?.value
            try await waitForRuleRequest()
            try Task.checkCancellation()
            guard self.context === context, context.isLoaded, isCurrent(), expiration > Date() else {
                throw CancellationError()
            }
            permissionExpiryTask?.cancel()
            // The public JS API needs its existing consent while removing rules.
            // Do not revoke it until WebKit confirms the empty ruleset.
            if choice != .allow {
                try await updateRules(enabled: false, context: context, isCurrent: isCurrent)
            }
            try Task.checkCancellation()
            guard self.context === context, isCurrent(), expiration > Date() else {
                throw CancellationError()
            }
            applySiteChoice(choice, pattern: pattern, expiration: expiration, context: context)
            guard context.hasAccess(to: url) == (choice == .allow) else {
                throw SiteError.invalidStatus
            }
            if choice == .allow {
                try await updateRules(enabled: true, context: context, isCurrent: isCurrent)
            } else if !context.hasPermission(.nativeMessaging) {
                disconnectRulePort()
            }
            try Task.checkCancellation()
            guard self.context === context, isCurrent(), expiration > Date() else {
                throw CancellationError()
            }
            schedulePermissionExpiry(context)
        } catch {
            failClosedRules(context)
            throw error
        }
    }

    private func applySiteChoice(_ choice: SiteChoice, pattern: WKWebExtension.MatchPattern,
                                 expiration: Date, context: WKWebExtensionContext) {
        // Remove broader AND narrower overlaps before checking effective status.
        context.grantedPermissionMatchPatterns = context.grantedPermissionMatchPatterns.filter {
            !pattern.matches($0.key, options: .matchBidirectionally)
        }
        context.deniedPermissionMatchPatterns = context.deniedPermissionMatchPatterns.filter {
            !pattern.matches($0.key, options: .matchBidirectionally)
        }
        let status: WKWebExtensionContext.PermissionStatus = choice == .allow
            ? .grantedExplicitly : .deniedExplicitly
        context.setPermissionStatus(status, for: pattern, expirationDate: expiration)
        if choice == .allow {
            var grants = context.grantedPermissions
            for permission in context.webExtension.requestedPermissions {
                // These are context-wide APIs, disclosed in the same native consent.
                // Replace the Date too: grantPermissions can retain an existing
                // entry's earlier expiry when only its granted status is set.
                grants[permission] = expiration
            }
            context.grantedPermissions = grants
        } else if context.grantedPermissionMatchPatterns.isEmpty {
            for permission in context.webExtension.requestedPermissions {
                context.setPermissionStatus(.deniedExplicitly, for: permission,
                                            expirationDate: expiration)
            }
        }
    }

    private func failClosedRules(_ context: WKWebExtensionContext) {
        guard self.context === context else { return }
        permissionExpiryTask?.cancel()
        permissionExpiryTask = nil
        ruleSuspensionTask?.cancel()
        ruleSuspensionRequested = false
        disconnectRulePort()
        context.grantedPermissionMatchPatterns = [:]
        context.grantedPermissions = [:]
        guard rulesMayBeEnabled else { return }
        // A lost/late acknowledgement cannot prove that compiled rules vanished.
        // This error path sacrifices memory-only storage; it is never a PASS.
        host.invalidate(context: context)
        do {
            if context.isLoaded { try controller?.unload(context) }
            host.didUnload(context: context)
        } catch { NSLog("Ahoi Spike fail-closed unload failed") }
        state = .failed("Unconfirmed rule removal; context unloaded, storage not preserved")
    }

    func reconcileExpiredRules() {
        guard let context, rulesMayBeEnabled,
              !context.hasPermission(.declarativeNetRequestWithHostAccess)
                || context.grantedPermissionMatchPatterns.isEmpty else { return }
        failClosedRules(context)
    }

    @discardableResult
    func suspendRuleControl() -> Task<Void, Never>? {
        guard let context, context.isLoaded, rulesMayBeEnabled else {
            ruleSuspensionRequested = false
            return nil
        }
        ruleSuspensionRequested = true
        guard !permissionChangeInProgress, ruleSuspensionTask == nil else { return ruleSuspensionTask }
        let task = Task { [weak self] in
            guard let self else { return }
            defer {
                self.ruleSuspensionTask = nil
                if self.ruleSuspensionRequested { self.suspendRuleControl() }
                else if self.context === context, context.isLoaded, !self.permissionChangeInProgress {
                    self.schedulePermissionExpiry(context)
                }
            }
            do {
                try await self.waitForRuleRequest()
                try Task.checkCancellation()
                guard self.context === context, context.isLoaded else { return }
                self.ruleSuspensionRequested = false
                self.permissionExpiryTask?.cancel()
                self.permissionExpiryTask = nil
                try await self.updateRules(enabled: false, context: context)
            }
            catch { self.failClosedRules(context) }
        }
        ruleSuspensionTask = task
        return task
    }

    var ruleUpdateInProgress: Bool { ruleRequest != nil }

    func refreshRuleControl() async throws {
        guard let context else { throw CancellationError() }
        if rulesMayBeEnabled { try await updateRules(enabled: true, context: context) }
    }

    private func waitForRuleRequest() async throws {
        guard ruleRequest != nil else { return }
        try await withCheckedThrowingContinuation { continuation in
            guard var request = ruleRequest else { continuation.resume(); return }
            request.waiters.append(continuation)
            ruleRequest = request
        }
    }

    func connectRulePort(_ port: WKWebExtension.MessagePort, controller: WKWebExtensionController,
                         context: WKWebExtensionContext) throws {
        guard self.controller === controller, self.context === context, context.isLoaded,
              context.hasPermission(.nativeMessaging),
              port.applicationIdentifier == Self.ruleApplicationIdentifier,
              rulePort == nil, !port.isDisconnected else { throw CancellationError() }
        rulePort = port // SDK27 requires the host to retain the connection.
        port.messageHandler = { [weak self, weak port, weak context] message, error in
            MainActor.assumeIsolated {
                guard let self, let port, let context, self.rulePort === port,
                      self.context === context else { return }
                guard error == nil, context.hasPermission(.nativeMessaging),
                      let message = message as? [String: Any] else {
                    self.finishRuleRequest(.failure(SiteError.ruleUpdateFailed)); return
                }
                if message["ready"] as? Bool == true {
                    self.rulePortReady = true
                    self.sendRuleRequest(context: context)
                    return
                }
                guard let request = self.ruleRequest, message["id"] as? String == request.id else { return }
                guard request.isCurrent(), message["ok"] as? Bool == true,
                      message["enabled"] as? Bool == request.enabled,
                      !request.enabled || (context.hasPermission(.declarativeNetRequestWithHostAccess)
                        && !context.grantedPermissionMatchPatterns.isEmpty) else {
                    self.finishRuleRequest(.failure(SiteError.ruleUpdateFailed)); return
                }
                self.rulesMayBeEnabled = request.enabled
                self.finishRuleRequest(.success(()))
            }
        }
        port.disconnectHandler = { [weak self, weak port] _ in
            MainActor.assumeIsolated {
                guard let self, let port, self.rulePort === port else { return }
                self.rulePort = nil
                self.rulePortReady = false
                self.finishRuleRequest(.failure(SiteError.ruleUpdateFailed))
            }
        }
    }

    private func updateRules(enabled: Bool, context: WKWebExtensionContext,
                             isCurrent: @escaping @MainActor () -> Bool = { true }) async throws {
        try Task.checkCancellation()
        guard self.context === context, context.isLoaded, isCurrent() else { throw CancellationError() }
        if !enabled && !rulesMayBeEnabled { return }
        guard ruleRequest == nil else { throw SiteError.busy }
        guard context.hasPermission(.declarativeNetRequestWithHostAccess),
              context.hasPermission(.nativeMessaging) else { throw SiteError.invalidStatus }
        let id = UUID().uuidString
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                ruleRequest = RuleRequest(id: id, enabled: enabled, isCurrent: isCurrent,
                                          continuation: continuation)
                ruleTimeoutTask = Task { [weak self] in
                    do { try await Task.sleep(for: .seconds(8)) } catch { return }
                    guard let self, self.ruleRequest?.id == id else { return }
                    self.finishRuleRequest(.failure(SiteError.ruleUpdateFailed))
                }
                if rulePortReady { sendRuleRequest(context: context) }
                else {
                    context.loadBackgroundContent { [weak self, weak context] error in
                        MainActor.assumeIsolated {
                            guard let self, let context, self.context === context,
                                  self.ruleRequest?.id == id, let error else { return }
                            self.finishRuleRequest(.failure(error))
                        }
                    }
                }
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                guard self?.ruleRequest?.id == id else { return }
                self?.finishRuleRequest(.failure(CancellationError()))
            }
        }
    }

    private func sendRuleRequest(context: WKWebExtensionContext) {
        guard var request = ruleRequest, !request.sent, let rulePort, rulePortReady else { return }
        guard self.context === context, request.isCurrent(),
              context.hasPermission(.nativeMessaging),
              context.hasPermission(.declarativeNetRequestWithHostAccess) else {
            finishRuleRequest(.failure(CancellationError())); return
        }
        request.sent = true
        ruleRequest = request
        if request.enabled { rulesMayBeEnabled = true }
        let id = request.id
        rulePort.sendMessage(["id": id, "enabled": request.enabled]) { [weak self] error in
            MainActor.assumeIsolated {
                guard let self, self.ruleRequest?.id == id, let error else { return }
                self.finishRuleRequest(.failure(error))
            }
        }
    }

    private func finishRuleRequest(_ result: Result<Void, any Error>) {
        guard let request = ruleRequest else { return }
        ruleRequest = nil
        ruleTimeoutTask?.cancel()
        ruleTimeoutTask = nil
        request.continuation.resume(with: result) // Clear before reentry/disconnect.
        for waiter in request.waiters { waiter.resume(with: result) }
    }

    private func disconnectRulePort() {
        let previous = rulePort
        rulePort = nil
        rulePortReady = false
        previous?.messageHandler = nil
        previous?.disconnectHandler = nil
        previous?.disconnect()
        finishRuleRequest(.failure(CancellationError()))
    }

    private func schedulePermissionExpiry(_ context: WKWebExtensionContext) {
        permissionExpiryTask?.cancel()
        let dates = Array(context.grantedPermissions.values) + Array(context.grantedPermissionMatchPatterns.values)
        guard let expiry = dates.min() else { permissionExpiryTask = nil; return }
        permissionExpiryTask = Task { [weak self, weak context] in
            do {
                if expiry.timeIntervalSinceNow > 130 {
                    // SDK background ports can become idle after two minutes.
                    // One acknowledged refresh keeps this 180-second DEBUG
                    // consent usable until removal; it never grants/renews rights.
                    try await Task.sleep(for: .seconds(max(0, expiry.timeIntervalSinceNow - 100)))
                    guard !Task.isCancelled, let self, let context, self.context === context else { return }
                    try await self.refreshRuleControl()
                }
                // Conservative DEBUG-only lead time: the public DNR API must
                // still be allowed while deleting its compiled rule cache.
                try await Task.sleep(for: .seconds(max(0, expiry.timeIntervalSinceNow - 10)))
                guard !Task.isCancelled, let self, let context, self.context === context else { return }
                try await self.updateRules(enabled: false, context: context)
                try await Task.sleep(for: .seconds(max(0, expiry.timeIntervalSinceNow)))
                guard !Task.isCancelled, self.context === context else { return }
                // The getters remove expired entries; no local grant cache/clock.
                _ = context.grantedPermissions
                _ = context.grantedPermissionMatchPatterns
                if !context.hasPermission(.nativeMessaging) { self.disconnectRulePort() }
                self.schedulePermissionExpiry(context)
            } catch is CancellationError { }
            catch {
                guard let self, let context else { return }
                self.failClosedRules(context)
            }
        }
    }
#endif

    private final class BundleMarker {}

    private static var resourceBundle: Bundle {
        #if SWIFT_PACKAGE
        Bundle.module
        #else
        Bundle(for: BundleMarker.self)
        #endif
    }
}
