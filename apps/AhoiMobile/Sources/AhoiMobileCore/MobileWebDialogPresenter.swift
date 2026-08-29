import Combine
import Foundation
import WebKit

public enum MobileJavaScriptDialogKind: Equatable, Sendable {
    case alert(message: String)
    case confirm(message: String)
    case prompt(message: String, defaultText: String?)

    public var message: String {
        switch self {
        case .alert(let message), .confirm(let message), .prompt(let message, _):
            return message
        }
    }
}

public struct MobileJavaScriptDialogRequest: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let origin: String
    public let isMainFrame: Bool
    public let kind: MobileJavaScriptDialogKind

    public init(
        id: UUID = UUID(),
        origin: String,
        isMainFrame: Bool,
        kind: MobileJavaScriptDialogKind
    ) {
        self.id = id
        self.origin = origin
        self.isMainFrame = isMainFrame
        self.kind = kind
    }
}

public struct MobileFileInputRequest: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let origin: String
    public let isMainFrame: Bool
    public let allowsMultipleSelection: Bool
    public let allowsDirectories: Bool

    public init(
        id: UUID = UUID(),
        origin: String,
        isMainFrame: Bool,
        allowsMultipleSelection: Bool,
        allowsDirectories: Bool
    ) {
        self.id = id
        self.origin = origin
        self.isMainFrame = isMainFrame
        self.allowsMultipleSelection = allowsMultipleSelection
        self.allowsDirectories = allowsDirectories
    }
}

/// Bridges WebKit's async dialog contract to native SwiftUI alert and file-importer
/// presentation owned by the browser view. Keep one presenter alive for each WebPage.
@MainActor
public final class MobileWebDialogPresenter: ObservableObject, WebPage.DialogPresenting {
    @Published public private(set) var pendingJavaScriptDialog: MobileJavaScriptDialogRequest?
    @Published public private(set) var pendingFileInput: MobileFileInputRequest?

    private enum PendingResolution {
        case alert(UUID, CheckedContinuation<Void, Never>)
        case confirm(UUID, CheckedContinuation<WebPage.JavaScriptConfirmResult, Never>)
        case prompt(UUID, CheckedContinuation<WebPage.JavaScriptPromptResult, Never>)
        case fileInput(
            UUID,
            allowsMultipleSelection: Bool,
            CheckedContinuation<WebPage.FileInputPromptResult, Never>
        )

        var id: UUID {
            switch self {
            case .alert(let id, _),
                 .confirm(let id, _),
                 .prompt(let id, _),
                 .fileInput(let id, _, _):
                return id
            }
        }
    }

    private var pendingResolution: PendingResolution?

    public init() {}

    public func handleJavaScriptAlert(
        message: String,
        initiatedBy frame: WebPage.FrameInfo
    ) async {
        let request = makeJavaScriptRequest(kind: .alert(message: message), frame: frame)
        cancelPending()
        await withTaskCancellationHandler {
            await withCheckedContinuation { continuation in
                pendingJavaScriptDialog = request
                pendingResolution = .alert(request.id, continuation)
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                self?.cancel(requestID: request.id)
            }
        }
    }

    public func handleJavaScriptConfirm(
        message: String,
        initiatedBy frame: WebPage.FrameInfo
    ) async -> WebPage.JavaScriptConfirmResult {
        let request = makeJavaScriptRequest(kind: .confirm(message: message), frame: frame)
        cancelPending()
        return await withTaskCancellationHandler {
            await withCheckedContinuation { continuation in
                pendingJavaScriptDialog = request
                pendingResolution = .confirm(request.id, continuation)
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                self?.cancel(requestID: request.id)
            }
        }
    }

    public func handleJavaScriptPrompt(
        message: String,
        defaultText: String?,
        initiatedBy frame: WebPage.FrameInfo
    ) async -> WebPage.JavaScriptPromptResult {
        let request = makeJavaScriptRequest(
            kind: .prompt(message: message, defaultText: defaultText),
            frame: frame
        )
        cancelPending()
        return await withTaskCancellationHandler {
            await withCheckedContinuation { continuation in
                pendingJavaScriptDialog = request
                pendingResolution = .prompt(request.id, continuation)
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                self?.cancel(requestID: request.id)
            }
        }
    }

    public func handleFileInputPrompt(
        parameters: WKOpenPanelParameters,
        initiatedBy frame: WebPage.FrameInfo
    ) async -> WebPage.FileInputPromptResult {
        let request = MobileFileInputRequest(
            origin: MobileBrowserOriginFormatter.label(
                for: frame.securityOrigin,
                fallbackURL: frame.request.url
            ),
            isMainFrame: frame.isMainFrame,
            allowsMultipleSelection: parameters.allowsMultipleSelection,
            allowsDirectories: parameters.allowsDirectories
        )
        cancelPending()
        return await withTaskCancellationHandler {
            await withCheckedContinuation { continuation in
                pendingFileInput = request
                pendingResolution = .fileInput(
                    request.id,
                    allowsMultipleSelection: request.allowsMultipleSelection,
                    continuation
                )
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                self?.cancel(requestID: request.id)
            }
        }
    }

    public func acknowledgeAlert(requestID: UUID) {
        guard case .alert(_, let continuation)? = takeResolution(requestID: requestID) else {
            return
        }
        continuation.resume()
    }

    public func respondToConfirmation(requestID: UUID, confirmed: Bool) {
        guard case .confirm(_, let continuation)? = takeResolution(requestID: requestID) else {
            return
        }
        continuation.resume(returning: confirmed ? .ok : .cancel)
    }

    public func respondToPrompt(requestID: UUID, submittedText: String) {
        guard case .prompt(_, let continuation)? = takeResolution(requestID: requestID) else {
            return
        }
        continuation.resume(returning: .ok(submittedText))
    }

    public func selectFiles(requestID: UUID, urls: [URL]) {
        guard case .fileInput(_, let allowsMultipleSelection, let continuation)? =
            takeResolution(requestID: requestID) else {
            return
        }
        let localFiles = urls.filter(\.isFileURL)
        let selection = allowsMultipleSelection
            ? localFiles
            : Array(localFiles.prefix(1))
        continuation.resume(
            returning: selection.isEmpty ? .cancel : .selected(selection)
        )
    }

    public func cancel(requestID: UUID) {
        guard pendingResolution?.id == requestID else { return }
        cancelPending()
    }

    public func cancelPending() {
        guard let resolution = pendingResolution else {
            pendingJavaScriptDialog = nil
            pendingFileInput = nil
            return
        }
        pendingResolution = nil
        pendingJavaScriptDialog = nil
        pendingFileInput = nil

        switch resolution {
        case .alert(_, let continuation):
            continuation.resume()
        case .confirm(_, let continuation):
            continuation.resume(returning: .cancel)
        case .prompt(_, let continuation):
            continuation.resume(returning: .cancel)
        case .fileInput(_, _, let continuation):
            continuation.resume(returning: .cancel)
        }
    }

    private func makeJavaScriptRequest(
        kind: MobileJavaScriptDialogKind,
        frame: WebPage.FrameInfo
    ) -> MobileJavaScriptDialogRequest {
        MobileJavaScriptDialogRequest(
            origin: MobileBrowserOriginFormatter.label(
                for: frame.securityOrigin,
                fallbackURL: frame.request.url
            ),
            isMainFrame: frame.isMainFrame,
            kind: kind
        )
    }

    private func takeResolution(requestID: UUID) -> PendingResolution? {
        guard pendingResolution?.id == requestID else { return nil }
        let resolution = pendingResolution
        pendingResolution = nil
        pendingJavaScriptDialog = nil
        pendingFileInput = nil
        return resolution
    }
}
