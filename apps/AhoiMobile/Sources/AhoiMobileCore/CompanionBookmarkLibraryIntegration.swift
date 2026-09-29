import Foundation
import SwiftUI

struct MobileBookmarkCapture: Identifiable {
    let id = UUID()
    let title: String
    let url: String
}

struct CompanionBookmarkLibraryEntry: View {
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(CompanionL10n.string("bookmark.library.title", fallback: "Bookmarks"),
                  systemImage: "bookmark")
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
        }
        .accessibilityIdentifier("bookmark.library.open")
    }
}
