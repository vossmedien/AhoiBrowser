import Foundation
import WebKit

@MainActor
public final class MobileDownloadCoordinator: NSObject, ObservableObject, WKDownloadDelegate {
    @Published public private(set) var downloads: [MobileDownloadRecord] = []
    @Published public private(set) var recoveryErrorMessage: String?

    private let directoryURL: URL
    private let recoveryWriter: MobileDownloadRecoveryWriter
    private var webViews: [UUID: WKWebView] = [:]
    private var activeDownloads: [UUID: WKDownload] = [:]
    private var recordIDByDownload: [ObjectIdentifier: UUID] = [:]
    private var progressObservations: [UUID: NSKeyValueObservation] = [:]
    private var persistedProgressBuckets: [UUID: Int] = [:]
    private var retryContexts: [UUID: MobileDownloadRetryContext] = [:]
    private var pendingRecoverySnapshot: [MobileDownloadRecord]?
    private var recoveryPersistenceTask: Task<Void, Never>?
    private var recoveryLoadTask: Task<[MobileDownloadRecord], Never>?
    private var didLoadRecoveryState = false

    public convenience init(directoryURL: URL) {
        let normalizedDirectory = directoryURL.standardizedFileURL
        self.init(
            directoryURL: normalizedDirectory,
            recoveryStore: MobileDownloadRecoveryStore(
                fileURL: normalizedDirectory.appendingPathComponent(
                    ".ahoi-download-recovery-v1.json",
                    isDirectory: false
                ),
                downloadDirectoryURL: normalizedDirectory
            )
        )
    }

    public convenience override init() {
        let fileManager = FileManager.default
        let documents = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let applicationSupport = fileManager.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        )[0]
        let directoryURL = documents.appendingPathComponent("Downloads", isDirectory: true)
        let recoveryURL = applicationSupport
            .appendingPathComponent("AhoiBrowser", isDirectory: true)
            .appendingPathComponent("DownloadRecovery", isDirectory: true)
            .appendingPathComponent("normal-v1.json", isDirectory: false)
        self.init(
            directoryURL: directoryURL,
            recoveryStore: MobileDownloadRecoveryStore(
                fileURL: recoveryURL,
                downloadDirectoryURL: directoryURL
            )
        )
    }

    init(directoryURL: URL, recoveryStore: MobileDownloadRecoveryStore) {
        self.directoryURL = directoryURL.standardizedFileURL
        self.recoveryWriter = MobileDownloadRecoveryWriter(store: recoveryStore)
        super.init()
    }

    /// Restores the bounded normal-download archive away from the main actor.
    /// Runtime records created before completion win by identifier, so a slow
    /// recovery read can never overwrite a newly initiated download.
    public func loadRecoveryState() async {
        guard !didLoadRecoveryState else { return }
        if recoveryLoadTask == nil {
            let writer = recoveryWriter
            recoveryLoadTask = Task {
                await writer.restoreRecords()
            }
        }
        guard let recoveryLoadTask else { return }
        let restored = await recoveryLoadTask.value
        guard !didLoadRecoveryState else { return }
        let runtimeIDs = Set(downloads.map(\.id))
        downloads = restored.filter { !runtimeIDs.contains($0.id) } + downloads
        didLoadRecoveryState = true
        self.recoveryLoadTask = nil
        persistRecoveryState()
    }

    public func start(
        request: URLRequest,
        websiteDataStore: WKWebsiteDataStore,
        initiatingOrigin: String? = nil,
        isPrivate: Bool
    ) {
        guard let url = request.url,
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else { return }
        let id = UUID()
        downloads.insert(MobileDownloadRecord(
            id: id,
            sourceURL: url,
            sourceOrigin: initiatingOrigin,
            suggestedFilename: Self.safeFilename(url.lastPathComponent, fallback: "download"),
            isPrivate: isPrivate
        ), at: 0)
        if let retryContext = MobileDownloadRetryContext(
            request: request,
            websiteDataStore: websiteDataStore,
            initiatingOrigin: initiatingOrigin,
            isPrivate: isPrivate
        ) {
            retryContexts[id] = retryContext
        }
        persistRecoveryState()

        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = websiteDataStore
        let webView = WKWebView(frame: .zero, configuration: configuration)
        webViews[id] = webView
        webView.startDownload(using: request) { [weak self] download in
            guard let self else { return }
            self.activeDownloads[id] = download
            self.recordIDByDownload[ObjectIdentifier(download)] = id
            guard self.downloads.contains(where: {
                $0.id == id && $0.status != .cancelled
            }) else {
                download.cancel { [weak self] _ in
                    Task { @MainActor in
                        self?.finish(id: id, download: download)
                    }
                }
                return
            }
            download.delegate = self
            self.update(id) { $0.status = .downloading }
            self.observeProgress(of: download, id: id)
        }
    }

    /// A failed normal download can be attempted again only while its original
    /// request and WebKit data store still exist in this process. No request
    /// descriptor or opaque resume data is written to the recovery archive.
    public func canRetry(_ id: UUID) -> Bool {
        guard let record = downloads.first(where: { $0.id == id }) else { return false }
        return record.status == .failed && !record.isPrivate && retryContexts[id] != nil
    }

    /// Starts a fresh WebKit download with the original safe request and data
    /// store. This deliberately creates a new attempt instead of reusing
    /// opaque `WKDownload` resume data or claiming byte-range continuation.
    @discardableResult
    public func retry(_ id: UUID) -> Bool {
        guard canRetry(id), let context = retryContexts[id] else { return false }
        discardRuntimeAttempt(id)
        retryContexts.removeValue(forKey: id)
        downloads.removeAll { $0.id == id }
        start(
            request: context.request,
            websiteDataStore: context.websiteDataStore,
            initiatingOrigin: context.initiatingOrigin,
            isPrivate: false
        )
        return true
    }

    public func recordFailure(
        sourceURL: URL,
        initiatingOrigin: String? = nil,
        isPrivate: Bool,
        message: String
    ) {
        guard (try? MobileBrowserInputRouter.validateWebURL(sourceURL)) != nil else {
            return
        }
        _ = message
        downloads.insert(MobileDownloadRecord(
            sourceURL: sourceURL,
            sourceOrigin: initiatingOrigin,
            suggestedFilename: Self.safeFilename(
                sourceURL.lastPathComponent,
                fallback: "download"
            ),
            status: .failed,
            errorMessage: MobileDownloadFailurePresentation.message(for: .policy),
            isPrivate: isPrivate
        ), at: 0)
        persistRecoveryState()
    }

    public func cancel(_ id: UUID) {
        guard downloads.contains(where: {
            $0.id == id && ($0.status == .starting || $0.status == .downloading)
        }) else { return }
        update(id) { $0.status = .cancelled }
        retryContexts.removeValue(forKey: id)
        guard let download = activeDownloads[id] else {
            progressObservations.removeValue(forKey: id)?.invalidate()
            persistedProgressBuckets.removeValue(forKey: id)
            webViews.removeValue(forKey: id)
            return
        }
        download.cancel { [weak self] _ in
            Task { @MainActor in
                self?.finish(id: id, download: download)
            }
        }
    }

    public func removeFinished() {
        let removedIDs = Set(downloads.lazy.filter {
            [.completed, .failed, .cancelled].contains($0.status)
        }.map(\.id))
        downloads.removeAll { removedIDs.contains($0.id) }
        retryContexts = retryContexts.filter { !removedIDs.contains($0.key) }
        persistRecoveryState()
    }

    public func removeFinished(isPrivate: Bool) {
        let removedIDs = Set(downloads.lazy.filter {
            $0.isPrivate == isPrivate &&
                [.completed, .failed, .cancelled].contains($0.status)
        }.map(\.id))
        downloads.removeAll { removedIDs.contains($0.id) }
        retryContexts = retryContexts.filter { !removedIDs.contains($0.key) }
        persistRecoveryState()
    }

    /// Ends the in-memory private-download lifetime with the private browsing
    /// session. Completed files remain user-owned, but no private request,
    /// progress, error, WebView, or download object may be projected into a
    /// later private session.
    public func endPrivateSession() {
        let privateIDs = Set(downloads.lazy.filter(\.isPrivate).map(\.id))
        guard !privateIDs.isEmpty else { return }

        let downloadsToCancel = privateIDs.compactMap { activeDownloads[$0] }
        for id in privateIDs {
            progressObservations.removeValue(forKey: id)?.invalidate()
            persistedProgressBuckets.removeValue(forKey: id)
            activeDownloads.removeValue(forKey: id)
            webViews.removeValue(forKey: id)
            retryContexts.removeValue(forKey: id)
        }
        recordIDByDownload = recordIDByDownload.filter {
            !privateIDs.contains($0.value)
        }
        downloads.removeAll { $0.isPrivate }
        persistRecoveryState()

        // WKDownload resume data is intentionally discarded: it can contain
        // cookies, credentials, headers, or other request material.
        for download in downloadsToCancel {
            download.cancel { _ in }
        }
    }

    /// Drains the coalesced recovery writer before a background transition or
    /// an exact-candidate test exits. File I/O remains off the main actor.
    public func flushRecoveryState() async {
        await loadRecoveryState()
        persistRecoveryState()
        while let task = recoveryPersistenceTask {
            await task.value
        }
    }

    public func download(
        _ download: WKDownload,
        decideDestinationUsing response: URLResponse,
        suggestedFilename: String
    ) async -> URL? {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return nil }
        guard downloads.first(where: { $0.id == id }).map({
            $0.status == .starting || $0.status == .downloading
        }) == true else { return nil }
        do {
            try FileManager.default.createDirectory(
                at: directoryURL,
                withIntermediateDirectories: true
            )
            let safeName = Self.safeFilename(suggestedFilename, fallback: "download")
            let destination = uniqueDestination(for: safeName)
            let accepted = applyRuntimeUpdate(
                .destination(
                    filename: destination.lastPathComponent,
                    url: destination,
                    expectedContentLength: response.expectedContentLength
                ),
                to: id
            )
            return accepted ? destination : nil
        } catch {
            _ = applyRuntimeUpdate(
                .failed(message: MobileDownloadFailurePresentation.message(
                    for: .destination,
                    underlyingError: error
                )),
                to: id
            )
            return nil
        }
    }

    public func downloadDidFinish(_ download: WKDownload) {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return }
        _ = applyRuntimeUpdate(.finished, to: id)
        finish(id: id, download: download)
    }

    public func download(
        _ download: WKDownload,
        didFailWithError error: any Error,
        resumeData: Data?
    ) {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return }
        // WebKit resume data is an opaque request archive and can contain
        // credentials, cookies or headers. Keep it memory-only and deliberately
        // discard it rather than claiming a resumable download after relaunch.
        _ = resumeData
        _ = applyRuntimeUpdate(
            .failed(message: MobileDownloadFailurePresentation.message(
                for: .transfer,
                underlyingError: error
            )),
            to: id
        )
        finish(id: id, download: download)
    }

    @discardableResult
    private func applyRuntimeUpdate(
        _ update: MobileDownloadRuntimeUpdate,
        to id: UUID
    ) -> Bool {
        guard let index = downloads.firstIndex(where: { $0.id == id }),
              downloads[index].applyRuntimeUpdate(update) else {
            return false
        }
        persistRecoveryState()
        return true
    }

    private func update(
        _ id: UUID,
        persist: Bool = true,
        mutation: (inout MobileDownloadRecord) -> Void
    ) {
        guard let index = downloads.firstIndex(where: { $0.id == id }) else { return }
        mutation(&downloads[index])
        if persist { persistRecoveryState() }
    }

    private func observeProgress(of download: WKDownload, id: UUID) {
        let observation = download.progress.observe(
            \.fractionCompleted,
            options: [.initial, .new]
        ) { [weak self] progress, _ in
            let completedUnitCount = progress.completedUnitCount
            let totalUnitCount = progress.totalUnitCount
            let fractionCompleted = progress.isIndeterminate || totalUnitCount <= 0
                ? nil
                : progress.fractionCompleted
            Task { @MainActor [weak self] in
                self?.updateProgress(
                    id: id,
                    completedUnitCount: completedUnitCount,
                    totalUnitCount: totalUnitCount,
                    fractionCompleted: fractionCompleted
                )
            }
        }
        progressObservations[id] = observation
    }

    private func updateProgress(
        id: UUID,
        completedUnitCount: Int64,
        totalUnitCount: Int64,
        fractionCompleted: Double?
    ) {
        var shouldPersist = false
        update(id, persist: false) {
            guard $0.status == .starting || $0.status == .downloading else { return }
            $0.bytesReceived = max(completedUnitCount, 0)
            if totalUnitCount > 0 {
                $0.totalBytesExpected = totalUnitCount
            }
            $0.progressFraction = fractionCompleted.flatMap { fraction in
                fraction.isFinite ? min(max(fraction, 0), 1) : nil
            }
            let nextBucket = Self.progressPersistenceBucket(for: $0)
            if nextBucket > (persistedProgressBuckets[id] ?? -1) {
                persistedProgressBuckets[id] = nextBucket
                shouldPersist = true
            }
        }
        if shouldPersist { persistRecoveryState() }
    }

    private func finish(id: UUID, download: WKDownload) {
        progressObservations.removeValue(forKey: id)?.invalidate()
        persistedProgressBuckets.removeValue(forKey: id)
        activeDownloads.removeValue(forKey: id)
        webViews.removeValue(forKey: id)
        recordIDByDownload.removeValue(forKey: ObjectIdentifier(download))
        if downloads.first(where: { $0.id == id })?.status != .failed {
            retryContexts.removeValue(forKey: id)
        }
    }

    private func discardRuntimeAttempt(_ id: UUID) {
        progressObservations.removeValue(forKey: id)?.invalidate()
        persistedProgressBuckets.removeValue(forKey: id)
        webViews.removeValue(forKey: id)
        guard let download = activeDownloads.removeValue(forKey: id) else { return }
        recordIDByDownload.removeValue(forKey: ObjectIdentifier(download))
        download.cancel { _ in }
    }

    private func persistRecoveryState() {
        pendingRecoverySnapshot = downloads
        guard recoveryPersistenceTask == nil else { return }
        recoveryPersistenceTask = Task { [weak self] in
            await self?.drainRecoveryPersistenceQueue()
        }
    }

    private func drainRecoveryPersistenceQueue() async {
        while let snapshot = pendingRecoverySnapshot {
            pendingRecoverySnapshot = nil
            do {
                try await recoveryWriter.save(records: snapshot)
                recoveryErrorMessage = nil
            } catch {
                // Never surface file paths, request metadata, or platform error
                // details from this privacy-sensitive archive.
                recoveryErrorMessage = CompanionL10n.string(
                    "browser.download.recovery.failed",
                    fallback: "Download recovery could not be saved."
                )
            }
        }
        recoveryPersistenceTask = nil
    }

    private func uniqueDestination(for filename: String) -> URL {
        let reservedPaths = Set(downloads.compactMap(\.destinationURL).map {
            $0.standardizedFileURL.path
        })
        return Self.uniqueDestination(
            for: filename,
            directoryURL: directoryURL,
            reservedPaths: reservedPaths
        )
    }

    nonisolated static func uniqueDestination(
        for filename: String,
        directoryURL: URL,
        reservedPaths: Set<String>,
        fileExists: (String) -> Bool = FileManager.default.fileExists(atPath:)
    ) -> URL {
        let source = URL(fileURLWithPath: filename)
        let stem = source.deletingPathExtension().lastPathComponent
        let ext = source.pathExtension
        var candidate = directoryURL.appendingPathComponent(filename)
        var suffix = 2
        while reservedPaths.contains(candidate.standardizedFileURL.path) ||
                fileExists(candidate.path) {
            let nextName = ext.isEmpty ? "\(stem)-\(suffix)" : "\(stem)-\(suffix).\(ext)"
            candidate = directoryURL.appendingPathComponent(nextName)
            suffix += 1
        }
        return candidate
    }

    private static func progressPersistenceBucket(for record: MobileDownloadRecord) -> Int {
        if let fraction = record.progressFraction {
            return Int((min(max(fraction, 0), 1) * 20).rounded(.down))
        }
        return Int(min(record.bytesReceived / (5 * 1_024 * 1_024), Int64(Int.max)))
    }

    nonisolated public static func safeFilename(_ value: String, fallback: String) -> String {
        let normalized = value.replacingOccurrences(of: "\\", with: "/")
        let rawLeaf = normalized
            .split(separator: "/", omittingEmptySubsequences: true)
            .last
            .map(String.init) ?? ""
        let leaf = rawLeaf
            .replacingOccurrences(of: ":", with: "-")
            .unicodeScalars
            .filter { !CharacterSet.controlCharacters.contains($0) }
            .map(String.init)
            .joined()
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !leaf.isEmpty, leaf != ".", leaf != ".." else { return fallback }
        return String(leaf.prefix(180))
    }
}

private actor MobileDownloadRecoveryWriter {
    private let store: MobileDownloadRecoveryStore

    init(store: MobileDownloadRecoveryStore) {
        self.store = store
    }

    func restoreRecords() -> [MobileDownloadRecord] {
        store.restoreRecords()
    }

    func save(records: [MobileDownloadRecord]) throws {
        try store.save(records: records)
    }
}

struct MobileDownloadRecoveryStore: Sendable {
    private static let schemaVersion = 1
    private static let maximumArchiveBytes = 256 * 1_024
    private static let maximumRecords = 128

    let fileURL: URL
    let downloadDirectoryURL: URL

    func restoreRecords() -> [MobileDownloadRecord] {
        guard let data = boundedArchiveData(),
              let archive = try? JSONDecoder().decode(Archive.self, from: data),
              archive.schemaVersion == Self.schemaVersion else {
            return []
        }

        var seenIDs: Set<UUID> = []
        return archive.records.prefix(Self.maximumRecords).compactMap { entry in
            guard seenIDs.insert(entry.id).inserted else { return nil }
            return entry.restoredRecord(downloadDirectoryURL: downloadDirectoryURL)
        }
    }

    private func boundedArchiveData() -> Data? {
        let keys: Set<URLResourceKey> = [
            .fileSizeKey,
            .isRegularFileKey,
            .isSymbolicLinkKey,
        ]
        guard let values = try? fileURL.resourceValues(forKeys: keys),
              values.isRegularFile == true,
              values.isSymbolicLink != true,
              let fileSize = values.fileSize,
              fileSize <= Self.maximumArchiveBytes,
              let handle = try? FileHandle(forReadingFrom: fileURL) else {
            return nil
        }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: Self.maximumArchiveBytes + 1),
              data.count <= Self.maximumArchiveBytes else {
            return nil
        }
        return data
    }

    func save(records: [MobileDownloadRecord]) throws {
        let entries = Array(records.lazy.compactMap { record in
            RecoveryEntry(record: record, downloadDirectoryURL: downloadDirectoryURL)
        }.prefix(Self.maximumRecords))
        guard !entries.isEmpty else {
            if FileManager.default.fileExists(atPath: fileURL.path) {
                try FileManager.default.removeItem(at: fileURL)
            }
            return
        }

        let parentURL = fileURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(
            at: parentURL,
            withIntermediateDirectories: true
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        let data = try encoder.encode(Archive(
            schemaVersion: Self.schemaVersion,
            records: entries
        ))
        guard data.count <= Self.maximumArchiveBytes else {
            throw RecoveryError.archiveTooLarge
        }
        try data.write(to: fileURL, options: [.atomic])
        try protectRecoveryFile()
    }

    private func protectRecoveryFile() throws {
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        var mutableURL = fileURL
        try mutableURL.setResourceValues(values)
#if os(iOS)
        try FileManager.default.setAttributes(
            [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication],
            ofItemAtPath: fileURL.path
        )
#endif
    }
}

private extension MobileDownloadRecoveryStore {
    enum RecoveryError: Error {
        case archiveTooLarge
    }

    struct Archive: Codable, Sendable {
        let schemaVersion: Int
        let records: [RecoveryEntry]
    }

    struct RecoveryEntry: Codable, Sendable {
        enum State: String, Codable, Sendable {
            case active
            case completed
            case failed
        }

        let id: UUID
        let sourceOrigin: String
        let suggestedFilename: String
        let destinationFilename: String?
        let state: State
        let bytesReceived: Int64
        let totalBytesExpected: Int64?

        init?(record: MobileDownloadRecord, downloadDirectoryURL: URL) {
            guard !record.isPrivate,
                  record.status != .cancelled,
                  let origin = Self.sanitizedOrigin(
                      record.sourceOrigin,
                      fallbackURL: record.sourceURL
                  ) else {
                return nil
            }
            id = record.id
            sourceOrigin = origin
            suggestedFilename = MobileDownloadCoordinator.safeFilename(
                record.suggestedFilename,
                fallback: "download"
            )
            destinationFilename = Self.safeDestinationFilename(
                record.destinationURL,
                downloadDirectoryURL: downloadDirectoryURL
            )
            switch record.status {
            case .starting, .downloading:
                state = .active
            case .completed:
                state = .completed
            case .failed:
                state = .failed
            case .cancelled:
                return nil
            }
            bytesReceived = max(record.bytesReceived, 0)
            totalBytesExpected = record.totalBytesExpected.flatMap { $0 >= 0 ? $0 : nil }
        }

        func restoredRecord(downloadDirectoryURL: URL) -> MobileDownloadRecord? {
            guard let sourceURL = URL(string: sourceOrigin),
                  (try? MobileBrowserInputRouter.validateWebURL(sourceURL)) != nil else {
                return nil
            }
            let safeSuggestedFilename = MobileDownloadCoordinator.safeFilename(
                suggestedFilename,
                fallback: "download"
            )
            let destination = Self.safeRestoredDestination(
                destinationFilename,
                downloadDirectoryURL: downloadDirectoryURL
            )
            let hasSafeCompletedFile = state == .completed && destination.map {
                Self.isRegularNonSymbolicFile($0)
            } == true
            let restoredStatus: MobileDownloadStatus = hasSafeCompletedFile ? .completed : .failed
            return MobileDownloadRecord(
                id: id,
                sourceURL: sourceURL,
                sourceOrigin: sourceOrigin,
                suggestedFilename: safeSuggestedFilename,
                destinationURL: hasSafeCompletedFile ? destination : nil,
                status: restoredStatus,
                errorMessage: nil,
                bytesReceived: bytesReceived,
                totalBytesExpected: totalBytesExpected,
                progressFraction: hasSafeCompletedFile ? 1 : nil,
                isPrivate: false
            )
        }

        private static func sanitizedOrigin(_ value: String, fallbackURL: URL) -> String? {
            for candidate in [URL(string: value), fallbackURL] {
                guard let candidate,
                      let components = URLComponents(
                          url: candidate,
                          resolvingAgainstBaseURL: false
                      ),
                      let scheme = components.scheme?.lowercased(),
                      scheme == "https" || scheme == "http",
                      let host = components.host,
                      !host.isEmpty else {
                    continue
                }
                var origin = URLComponents()
                origin.scheme = scheme
                origin.host = host
                origin.port = components.port
                return origin.url?.absoluteString
            }
            return nil
        }

        private static func safeDestinationFilename(
            _ destinationURL: URL?,
            downloadDirectoryURL: URL
        ) -> String? {
            guard let destinationURL, destinationURL.isFileURL else {
                return nil
            }
            let lexicalBase = downloadDirectoryURL.standardizedFileURL
            guard destinationURL.deletingLastPathComponent().standardizedFileURL
                    == lexicalBase else {
                return nil
            }
            let safeName = MobileDownloadCoordinator.safeFilename(
                destinationURL.lastPathComponent,
                fallback: ""
            )
            guard !safeName.isEmpty,
                  safeName == destinationURL.lastPathComponent,
                  safeRestoredDestination(
                      safeName,
                      downloadDirectoryURL: downloadDirectoryURL,
                      requiresRegularFile: false
                  ) != nil else {
                return nil
            }
            return safeName
        }

        private static func safeRestoredDestination(
            _ rawFilename: String?,
            downloadDirectoryURL: URL,
            requiresRegularFile: Bool = true
        ) -> URL? {
            guard let rawFilename, !rawFilename.isEmpty else { return nil }
            let safeName = MobileDownloadCoordinator.safeFilename(
                rawFilename,
                fallback: ""
            )
            guard safeName == rawFilename else { return nil }

            let lexicalBase = downloadDirectoryURL.standardizedFileURL
            let candidate = lexicalBase
                .appendingPathComponent(safeName, isDirectory: false)
                .standardizedFileURL
            guard candidate.deletingLastPathComponent() == lexicalBase else {
                return nil
            }

            let resolvedBase = lexicalBase.resolvingSymlinksInPath()
            let resolvedCandidate = candidate.resolvingSymlinksInPath()
            guard resolvedCandidate.deletingLastPathComponent() == resolvedBase else {
                return nil
            }
            guard !requiresRegularFile || isRegularNonSymbolicFile(candidate) else {
                return nil
            }
            return candidate
        }

        private static func isRegularNonSymbolicFile(_ url: URL) -> Bool {
            guard let values = try? url.resourceValues(forKeys: [
                .isRegularFileKey,
                .isSymbolicLinkKey,
            ]) else {
                return false
            }
            return values.isRegularFile == true && values.isSymbolicLink != true
        }
    }
}
