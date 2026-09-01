import Foundation

enum MobileDownloadRuntimeUpdate: Equatable, Sendable {
    case destination(
        filename: String,
        url: URL,
        expectedContentLength: Int64
    )
    case finished
    case failed(message: String)
}

extension MobileDownloadRecord {
    /// WebKit may deliver destination or terminal callbacks after cancellation.
    /// Runtime updates are monotonic: only an active record may advance, and a
    /// terminal user-visible state can never be replaced by a later callback.
    @discardableResult
    mutating func applyRuntimeUpdate(_ update: MobileDownloadRuntimeUpdate) -> Bool {
        guard status == .starting || status == .downloading else { return false }
        switch update {
        case let .destination(filename, url, expectedContentLength):
            suggestedFilename = filename
            destinationURL = url
            if expectedContentLength >= 0 {
                totalBytesExpected = expectedContentLength
            }
        case .finished:
            guard destinationURL != nil else {
                status = .failed
                errorMessage = MobileDownloadFailurePresentation.message(for: .destination)
                progressFraction = nil
                return true
            }
            status = .completed
            if let totalBytesExpected {
                bytesReceived = max(bytesReceived, totalBytesExpected)
            }
            progressFraction = 1
        case let .failed(message):
            status = .failed
            errorMessage = message
        }
        return true
    }
}
