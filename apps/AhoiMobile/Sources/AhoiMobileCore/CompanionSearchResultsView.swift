import Foundation
import SwiftUI
import AhoiCloudKitSpike

struct CompanionSearchResultsView: View {
    let results: [CompanionSearchResult]
    let openURL: OpenURLAction
    let onOpenTreeNode: ((TreeNodeID) -> Void)?
    @State private var unsupportedAddress = false

    var body: some View {
        List(results) { result in
            Button {
                if result.kind == .savedPage, let onOpenTreeNode {
                    onOpenTreeNode(TreeNodeID(rawValue: result.id))
                    return
                }
                guard let url = result.url.flatMap(URL.init(string:)) else { return }
                if result.kind == .bookmark,
                   (try? MobileBrowserInputRouter.validateWebURL(url)) == nil {
                    unsupportedAddress = true
                    return
                }
                openURL(url)
            } label: {
                VStack(alignment: .leading, spacing: 3) {
                    Text(result.title)
                    Text(result.detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.plain)
            .disabled(result.url == nil)
            .accessibilityIdentifier(
                "browser.library.search-result.\(result.kind.rawValue).\(result.id.uuidString.lowercased())"
            )
        }
        .accessibilityIdentifier("browser.library.search-results")
        .navigationTitle(CompanionL10n.string("search.title", fallback: "Search"))
        .alert(CompanionL10n.string("bookmark.unsupported.title", fallback: "This address cannot be opened here"),
               isPresented: $unsupportedAddress) {
            Button(CompanionL10n.string("action.ok", fallback: "OK"), role: .cancel) {}
        } message: {
            Text(CompanionL10n.string(
                "bookmark.unsupported.message",
                fallback: "The address remains saved as bookmark metadata, but iPhone and iPad open only web addresses here."
            ))
        }
    }
}
