import Foundation
import SwiftUI
import WebKit

enum MobilePageLink {
    /// Explicit copy only: credentials are removed and non-web targets rejected.
    static func safeURL(_ url: URL?) -> URL? {
        guard let url, var parts = URLComponents(url: url, resolvingAgainstBaseURL: false),
              ["http", "https"].contains(parts.scheme?.lowercased() ?? ""),
              let host = parts.host, !host.isEmpty else { return nil }
        parts.user = nil
        parts.password = nil
        return parts.url
    }

    static func markdown(title: String, url: URL) -> String {
        let title = MobileTabRecord.normalizedTitle(title)
            .components(separatedBy: .whitespacesAndNewlines)
            .filter { !$0.isEmpty }.joined(separator: " ")
        let label = (title.isEmpty ? (url.host() ?? url.absoluteString) : title)
            .unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) }
            .map {
                "\\`*_{}[]<>()#+-.!|".unicodeScalars.contains($0)
                    ? "\\" + String($0)
                    : String($0)
            }
            .joined()
        // Angle-delimited destination plus percent-escaped delimiter/control bytes.
        var allowed = CharacterSet.urlFragmentAllowed
        allowed.insert(charactersIn: "%")
        allowed.remove(charactersIn: "<>\\\"() \t\r\n")
        let destination = url.absoluteString.addingPercentEncoding(withAllowedCharacters: allowed) ?? ""
        return "[\(label)](<\(destination)>)"
    }
}

struct MobileReaderArticle: Identifiable {
    let id = UUID()
    let title: String
    let origin: String
    let paragraphs: [String]
}

extension MobileBrowserController {
    /// Read only the live main document in an isolated world. No fetch, DOM
    /// rewrite, form values, subframes, external parser, or second WebPage.
    func selectedReaderArticle() async -> MobileReaderArticle? {
        guard let tabID = selectedTabID, let page = selectedPage, !page.isLoading,
              let sourceURL = page.url, MobilePageLink.safeURL(sourceURL) != nil else { return nil }
        let generation = navigationDocumentGenerations[tabID, default: 0]
        let script = #"""
        const roots = document.querySelectorAll('article, [role="article"], main');
        const excluded = [
          'nav', 'header', 'footer', 'aside', 'form', 'button', '[hidden]',
          '[aria-hidden="true"]', '[contenteditable]:not([contenteditable="false"])'
        ].join(',');
        let best = null;
        for (let rootIndex = 0; rootIndex < Math.min(roots.length, 32); rootIndex++) {
          const root = roots[rootIndex];
          if (root.closest(excluded)) continue;
          const paragraphs = [];
          let total = 0, prose = 0;
          const candidates = root.querySelectorAll('h1,h2,h3,p,li,blockquote,pre');
          for (let nodeIndex = 0; nodeIndex < Math.min(candidates.length, 1000); nodeIndex++) {
            const node = candidates[nodeIndex];
            if (node.closest(excluded) || node.parentElement?.closest('p,li,blockquote,pre')) continue;
            if (!node.checkVisibility({checkOpacity:true,checkVisibilityCSS:true})) continue;
            const text = (node.innerText || '').trim();
            if (!text) continue;
            // Avoid navigation/link directories and reject oversized documents rather than truncate.
            let linked = 0;
            for (const anchor of node.querySelectorAll('a')) linked += (anchor.innerText || '').length;
            if (linked > text.length * 0.5) continue;
            if (text.length > 20000 || total + text.length > 120000) return null;
            total += text.length;
            if (node.tagName === 'P' && text.length >= 80) prose++;
            paragraphs.push(text);
          }
          if (prose >= 2 && total >= 400 && (!best || total > best.total))
            best = {paragraphs, total};
        }
        return best ? {
          paragraphs: best.paragraphs,
          title: (document.title || '').slice(0, 2048),
          url: location.href
        } : null;
        """#
        let value = try? await page.callJavaScript(script, contentWorld: .world(name: "AhoiReader"))
        guard !Task.isCancelled, selectedTabID == tabID, pages[tabID] === page,
              navigationDocumentGenerations[tabID, default: 0] == generation,
              page.url == sourceURL, !page.isLoading,
              let result = value as? [String: Any], result["url"] as? String == sourceURL.absoluteString,
              let paragraphs = result["paragraphs"] as? [String], !paragraphs.isEmpty,
              paragraphs.count <= 1000, paragraphs.reduce(0, { $0 + $1.utf16.count }) <= 120000 else { return nil }
        let title = MobileTabRecord.normalizedTitle(result["title"] as? String ?? "")
        return MobileReaderArticle(
            title: title.isEmpty ? (sourceURL.host() ?? sourceURL.absoluteString) : title,
            origin: sourceURL.host() ?? "",
            paragraphs: paragraphs
        )
    }
}

struct MobileReaderView: View {
    let article: MobileReaderArticle
    let onReturn: () -> Void

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    Text(verbatim: article.origin).font(.caption).foregroundStyle(.secondary)
                    Text(verbatim: article.title).font(.largeTitle.bold()).accessibilityAddTraits(.isHeader)
                    ForEach(article.paragraphs.indices, id: \.self) { index in
                        Text(verbatim: article.paragraphs[index]).font(.system(.body, design: .serif))
                    }
                }
                .lineSpacing(6)
                .frame(maxWidth: 680, alignment: .leading)
                .padding(24)
                .frame(maxWidth: .infinity)
            }
            .navigationTitle(CompanionL10n.string("browser.reader", fallback: "Reader"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(
                        CompanionL10n.string("browser.reader.return", fallback: "Back to Page"),
                        action: onReturn
                    )
                        .accessibilityIdentifier("browser.reader.return")
                }
            }
        }
        .accessibilityIdentifier("browser.reader.content")
    }
}
