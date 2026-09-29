// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_LINK_PEEK_H_
#define AHOI_BROWSER_POPUP_LINK_PEEK_H_

#include <optional>
#include <string>

#include "content/public/common/referrer.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom-forward.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {
struct OpenURLParams;
class WebContents;
}  // namespace content

namespace ahoi::popup {

// Optional automatic Peek: a link from a saved page to another site opens
// as a preview instead of replacing the saved page. Off by default.
inline constexpr char kAutoPeekFromSavedPagesPref[] =
    "ahoi.peek.auto_from_saved_pages";
// Optional modifier: Shift-click on a link previews it instead of opening
// Chromium's new window. Off by default, so Shift-click keeps its meaning.
inline constexpr char kPeekOnShiftClickPref[] = "ahoi.peek.shift_click";

// The request a Peek loads: the intercepted link's own URL, referrer and
// initiator, so a preview sends exactly what the page's click would have
// sent (rel=noreferrer, Referrer-Policy, the clicked iframe's origin). A
// default request has no referrer and no initiator, like a typed address.
struct PeekRequest {
  PeekRequest();
  explicit PeekRequest(const GURL& url);
  PeekRequest(const PeekRequest&);
  PeekRequest& operator=(const PeekRequest&);
  ~PeekRequest();

  // What the command bar previews: a typed address or a search, sent like
  // the omnibox sends it, without referrer or initiator.
  static PeekRequest ForTypedUrl(const GURL& url, bool is_search);
  // A link Chromium was about to open elsewhere (Shift-click), with the
  // referrer and initiator the page sent.
  static PeekRequest FromOpenURLParams(const content::OpenURLParams& params);

  GURL url;
  // Sanitized again for `url` under its own policy before the load.
  content::Referrer referrer;
  std::optional<url::Origin> initiator_origin;
  ui::PageTransition transition = ui::PAGE_TRANSITION_LINK;
  bool started_from_context_menu = false;
};

// Link-Peek: a link opens as a short preview over its page, in a real
// WebContents owned by the window's popup overlay. Chromium's context menu
// reaches the overlay through this hook, so it needs no dependency on the
// browser window code. Each window's overlay registers itself as a host.
class LinkPeekHost {
 public:
  virtual ~LinkPeekHost() = default;
  // True when this host shows pages of `opener` and could preview `url` now.
  virtual bool CanPeek(content::WebContents* opener, const GURL& url) = 0;
  virtual bool ShowPeek(content::WebContents* opener,
                        const PeekRequest& request) = 0;
  // True when `contents` is a saved page of the tab tree.
  virtual bool IsSavedPage(content::WebContents* contents) = 0;
};

void AddLinkPeekHost(LinkPeekHost* host);
void RemoveLinkPeekHost(LinkPeekHost* host);

// Only http and https links are previewed.
bool IsPeekableUrl(const GURL& url);
bool CanPeekLink(content::WebContents* opener, const GURL& url);
bool PeekLink(content::WebContents* opener, const PeekRequest& request);
// Whether automatic Peek applies to this link of a saved page; the caller
// has already checked the pref, the user gesture and the other site.
bool CanAutoPeekLink(content::WebContents* opener, const GURL& url);

// The site rule of automatic Peek: a web page's link to another registrable
// domain. Links within the site keep navigating the saved page.
bool LeavesSite(const GURL& page_url, const GURL& link_url);

// A new window Chromium is about to add as a tab, as far as automatic Peek
// needs to know it.
struct NewWindowLink {
  GURL source_url;
  GURL target_url;
  WindowOpenDisposition disposition = WindowOpenDisposition::UNKNOWN;
  bool user_gesture = false;
  // window.open() and rel=opener links keep their script connection, so
  // they stay real tabs. A plain target=_blank link has no opener.
  bool has_opener = false;
  // False after a modifier click (Cmd+Shift-click also arrives as a
  // foreground tab) or when the source's input is unknown.
  bool plain_activation = false;
  // window.open() with popup or size features. Chromium on macOS turns such
  // a popup into a foreground tab in browser fullscreen; it stays one.
  bool popup_features = false;
};

// Whether a plain target=_blank click leaving the site may become a Peek.
// The caller still checks the pref and that the source is a saved page.
bool IsAutoPeekNewWindowCandidate(const NewWindowLink& link);

// The whole rule for a new window of `source` that Chromium is about to add
// as a tab, except the pref: a plain target=_blank click from a saved page
// to another site, while an overlay can show it.
bool ShouldAutoPeekNewWindow(content::WebContents* source,
                             content::WebContents* new_contents,
                             const GURL& target_url,
                             WindowOpenDisposition disposition,
                             const blink::mojom::WindowFeatures& features,
                             bool user_gesture);

// The context-menu label in the browser's language.
std::u16string PeekLinkMenuLabel();

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_LINK_PEEK_H_
