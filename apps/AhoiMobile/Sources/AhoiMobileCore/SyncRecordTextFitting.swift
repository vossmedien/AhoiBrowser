import Foundation
import AhoiCloudKitSpike

/// Writer side of `config/sync-format.json` `recordTextLimits` (Crest 77457bb,
/// review row A6). Readers keep rejecting oversized metadata, so every writer
/// fits the records it authors to the strictest reader: text is cut after the
/// last whole `Character` within the UTF-8 limit, and an address past its limit
/// is never cut, its record stays on this device.
public enum SyncRecordTextFitting {
    public static let maximumTitleUTF8Bytes = RemoteTab.maximumTitleUTF8Bytes
    public static let maximumDeviceNameUTF8Bytes = RemoteTab.maximumDeviceNameUTF8Bytes
    public static let maximumWorkspaceNameUTF8Bytes = RemoteTab.maximumWorkspaceNameUTF8Bytes
    public static let maximumHistoryURLUTF8Bytes = HistoryVisit.maximumURLUTF8Bytes
    public static let maximumHistoryTransitionUTF8Bytes = HistoryVisit.maximumTransitionUTF8Bytes

    /// `text` cut after the last whole character that keeps it within
    /// `maximumUTF8Bytes`. Text that already fits is returned unchanged.
    public static func fit(_ text: String, maximumUTF8Bytes: Int) -> String {
        guard text.utf8.count > maximumUTF8Bytes else { return text }
        var used = 0
        var end = text.startIndex
        var index = text.startIndex
        while index < text.endIndex {
            let next = text.index(after: index)
            let bytes = text.utf8[index..<next].count
            guard used + bytes <= maximumUTF8Bytes else { break }
            used += bytes
            end = next
            index = next
        }
        return String(text[..<end])
    }

    public static func title(_ text: String) -> String {
        fit(text, maximumUTF8Bytes: maximumTitleUTF8Bytes)
    }

    public static func deviceName(_ text: String) -> String {
        fit(text, maximumUTF8Bytes: maximumDeviceNameUTF8Bytes)
    }

    public static func workspaceName(_ text: String) -> String {
        fit(text, maximumUTF8Bytes: maximumWorkspaceNameUTF8Bytes)
    }

    public static func historyTransition(_ text: String) -> String {
        fit(text, maximumUTF8Bytes: maximumHistoryTransitionUTF8Bytes)
    }

    /// Whether every reader accepts this history address. A longer one is
    /// kept out of the shared history instead of being cut into another page.
    public static func historyURLFits(_ url: String) -> Bool {
        url.utf8.count <= maximumHistoryURLUTF8Bytes
    }
}
