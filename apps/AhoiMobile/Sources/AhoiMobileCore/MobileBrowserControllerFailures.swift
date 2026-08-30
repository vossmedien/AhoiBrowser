import Foundation
import WebKit

extension MobileBrowserController {
    public static func classifyNavigationFailure(_ error: Error) -> MobilePageFailureKind {
        if let navigationError = error as? WebPage.NavigationError {
            switch navigationError {
            case .failedProvisionalNavigation(let underlying):
                return classifyNavigationFailure(underlying)
            case .webContentProcessTerminated:
                return .webContentTerminated
            case .invalidURL:
                return .invalidURL
            case .pageClosed:
                return .failed
            @unknown default:
                return .failed
            }
        }

        let failure = error as NSError
        switch failure.code {
        case NSURLErrorNotConnectedToInternet, NSURLErrorNetworkConnectionLost:
            return .offline
        case NSURLErrorTimedOut:
            return .timedOut
        case NSURLErrorBadURL, NSURLErrorUnsupportedURL:
            return .invalidURL
        default:
            if let underlying = failure.userInfo[NSUnderlyingErrorKey] as? Error {
                return classifyNavigationFailure(underlying)
            }
            return .failed
        }
    }

    public func clearWebsiteData() async {
        let types = WKWebsiteDataStore.allWebsiteDataTypes()
        await withCheckedContinuation { continuation in
            WKWebsiteDataStore.default().removeData(
                ofTypes: types,
                modifiedSince: .distantPast
            ) {
                continuation.resume()
            }
        }
    }

}
