import Foundation

struct MobileStagedFileSelection: Sendable {
    let directoryURL: URL
    let selectedURLs: [URL]
}

enum MobileFileInputStagingError: Error {
    case emptySelection
    case unsupportedItem
}

/// File-provider URLs may require a security scope for their complete read.
/// WebKit does not expose an "upload consumed" callback, so forwarding those
/// URLs and immediately closing their scopes is inherently racy. Ahoi instead
/// performs a coordinated copy while the scope is held and gives WebKit only
/// app-owned staging URLs. The staging root contains no source path components
/// and remains alive for the process lifetime; stale roots are removed before
/// the first selection of the next launch.
@MainActor
final class MobileFileInputStagingStore {
    static let shared = MobileFileInputStagingStore()

    private let rootURL: URL
    private var retainedDirectories: Set<URL> = []

    private init(fileManager: FileManager = .default) {
        rootURL = fileManager.temporaryDirectory
            .appendingPathComponent("AhoiBrowserFileInput", isDirectory: true)
        try? fileManager.removeItem(at: rootURL)
        try? fileManager.createDirectory(
            at: rootURL,
            withIntermediateDirectories: true
        )
    }

    func stage(
        urls: [URL],
        for request: MobileFileInputRequest
    ) async throws -> MobileStagedFileSelection {
        let candidates = request.allowsMultipleSelection ? urls : Array(urls.prefix(1))
        guard !candidates.isEmpty else { throw MobileFileInputStagingError.emptySelection }
        let destination = rootURL.appendingPathComponent(
            request.id.uuidString.lowercased(),
            isDirectory: true
        )
        let selection = try await Task.detached(priority: .userInitiated) {
            try MobileFileInputStager.stage(
                candidates,
                at: destination
            )
        }.value
        do {
            try Task.checkCancellation()
            retainedDirectories.insert(selection.directoryURL)
            return selection
        } catch {
            try? FileManager.default.removeItem(at: selection.directoryURL)
            throw error
        }
    }

    func discard(_ selection: MobileStagedFileSelection) {
        retainedDirectories.remove(selection.directoryURL)
        try? FileManager.default.removeItem(at: selection.directoryURL)
    }
}

private enum MobileFileInputStager {
    static func stage(
        _ sourceURLs: [URL],
        at destinationDirectory: URL
    ) throws -> MobileStagedFileSelection {
        let fileManager = FileManager()
        try fileManager.createDirectory(
            at: destinationDirectory,
            withIntermediateDirectories: true
        )
        do {
            var selectedURLs: [URL] = []
            for (index, sourceURL) in sourceURLs.enumerated() {
                try Task.checkCancellation()
                guard sourceURL.isFileURL else {
                    throw MobileFileInputStagingError.unsupportedItem
                }
                let destinationURL = uniqueDestination(
                    for: sourceURL,
                    index: index,
                    in: destinationDirectory,
                    fileManager: fileManager
                )
                try copySecurityScopedItem(
                    at: sourceURL,
                    to: destinationURL,
                    fileManager: fileManager
                )
                selectedURLs.append(destinationURL)
            }
            guard !selectedURLs.isEmpty else {
                throw MobileFileInputStagingError.emptySelection
            }
            return MobileStagedFileSelection(
                directoryURL: destinationDirectory,
                selectedURLs: selectedURLs
            )
        } catch {
            try? fileManager.removeItem(at: destinationDirectory)
            throw error
        }
    }

    private static func copySecurityScopedItem(
        at sourceURL: URL,
        to destinationURL: URL,
        fileManager: FileManager
    ) throws {
        let acquiredScope = sourceURL.startAccessingSecurityScopedResource()
        defer {
            if acquiredScope {
                sourceURL.stopAccessingSecurityScopedResource()
            }
        }

        let coordinator = NSFileCoordinator(filePresenter: nil)
        var coordinationError: NSError?
        let copyResult = MobileCoordinatedCopyResultBox()
        coordinator.coordinate(
            readingItemAt: sourceURL,
            options: .withoutChanges,
            error: &coordinationError
        ) { coordinatedURL in
            copyResult.store(Result {
                try copyItemRecursively(
                    at: coordinatedURL,
                    to: destinationURL,
                    fileManager: fileManager
                )
            })
        }
        if let coordinationError { throw coordinationError }
        guard let result = copyResult.load() else {
            throw MobileFileInputStagingError.unsupportedItem
        }
        try result.get()
    }

    private static func copyItemRecursively(
        at sourceURL: URL,
        to destinationURL: URL,
        fileManager: FileManager
    ) throws {
        try Task.checkCancellation()
        let values = try sourceURL.resourceValues(forKeys: [
            .isDirectoryKey,
            .isRegularFileKey,
            .isSymbolicLinkKey,
        ])
        guard values.isSymbolicLink != true else {
            throw MobileFileInputStagingError.unsupportedItem
        }
        if values.isDirectory == true {
            try fileManager.createDirectory(
                at: destinationURL,
                withIntermediateDirectories: false
            )
            let children = try fileManager.contentsOfDirectory(
                at: sourceURL,
                includingPropertiesForKeys: [
                    .isDirectoryKey,
                    .isRegularFileKey,
                    .isSymbolicLinkKey,
                ],
                options: []
            )
            for (index, child) in children.enumerated() {
                try copyItemRecursively(
                    at: child,
                    to: uniqueDestination(
                        for: child,
                        index: index,
                        in: destinationURL,
                        fileManager: fileManager
                    ),
                    fileManager: fileManager
                )
            }
            return
        }
        guard values.isRegularFile == true else {
            throw MobileFileInputStagingError.unsupportedItem
        }
        try fileManager.copyItem(at: sourceURL, to: destinationURL)
    }

    private static func uniqueDestination(
        for sourceURL: URL,
        index: Int,
        in directoryURL: URL,
        fileManager: FileManager
    ) -> URL {
        let safeSourceName = safeName(sourceURL.lastPathComponent)
        let fallback = "selection-\(index + 1)"
        let baseName = safeSourceName.isEmpty ? fallback : safeSourceName
        var candidate = directoryURL.appendingPathComponent(baseName)
        var suffix = 2
        while fileManager.fileExists(atPath: candidate.path) {
            let stem = candidate.deletingPathExtension().lastPathComponent
            let pathExtension = candidate.pathExtension
            let suffixedName = pathExtension.isEmpty
                ? "\(stem)-\(suffix)"
                : "\(stem)-\(suffix).\(pathExtension)"
            candidate = directoryURL.appendingPathComponent(suffixedName)
            suffix += 1
        }
        return candidate
    }

    private static func safeName(_ value: String) -> String {
        let forbidden = CharacterSet.controlCharacters.union(
            CharacterSet(charactersIn: "/:\\")
        )
        var sanitized = ""
        for scalar in value.unicodeScalars where !forbidden.contains(scalar) {
            sanitized.unicodeScalars.append(scalar)
        }
        sanitized = sanitized.trimmingCharacters(in: .whitespacesAndNewlines)
        while sanitized.hasPrefix(".") {
            sanitized.removeFirst()
        }
        if sanitized.utf8.count > 160 {
            let pathExtension = URL(fileURLWithPath: sanitized).pathExtension
            let reservedBytes = min(pathExtension.utf8.count + 1, 24)
            let stemLimit = max(1, 160 - reservedBytes)
            sanitized = boundedUTF8(sanitized, maximumBytes: stemLimit)
            if !pathExtension.isEmpty {
                sanitized += "." + boundedUTF8(pathExtension, maximumBytes: 23)
            }
        }
        return sanitized
    }

    private static func boundedUTF8(_ value: String, maximumBytes: Int) -> String {
        var bytes = Data(value.utf8.prefix(maximumBytes))
        while !bytes.isEmpty, String(data: bytes, encoding: .utf8) == nil {
            bytes.removeLast()
        }
        return String(data: bytes, encoding: .utf8) ?? ""
    }
}

private final class MobileCoordinatedCopyResultBox: @unchecked Sendable {
    private let lock = NSLock()
    private var result: Result<Void, Error>?

    func store(_ result: Result<Void, Error>) {
        lock.lock()
        self.result = result
        lock.unlock()
    }

    func load() -> Result<Void, Error>? {
        lock.lock()
        defer { lock.unlock() }
        return result
    }
}

#if DEBUG
enum MobileFileInputUITestFixture {
    static func selectionURLsIfRequested() -> [URL]? {
        guard ProcessInfo.processInfo.arguments.contains("-AhoiUITestFileSelection") else {
            return nil
        }
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("AhoiMobileUITestInput", isDirectory: true)
        let url = directory.appendingPathComponent("ahoi-upload-fixture.txt")
        do {
            try FileManager.default.createDirectory(
                at: directory,
                withIntermediateDirectories: true
            )
            try Data("Ahoi upload fixture\n".utf8).write(to: url, options: .atomic)
            return [url]
        } catch {
            return []
        }
    }
}
#endif
