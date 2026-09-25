// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_LINK_PEEK_H_
#define AHOI_BROWSER_POPUP_LINK_PEEK_H_

#include <string>

class GURL;

namespace content {
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

// Link-Peek: a link opens as a short preview over its page, in a real
// WebContents owned by the window's popup overlay. Chromium's context menu
// reaches the overlay through this hook, so it needs no dependency on the
// browser window code. Each window's overlay registers itself as a host.
class LinkPeekHost {
 public:
  virtual ~LinkPeekHost() = default;
  // True when this host shows pages of `opener` and could preview `url` now.
  virtual bool CanPeek(content::WebContents* opener, const GURL& url) = 0;
  virtual bool ShowPeek(content::WebContents* opener, const GURL& url) = 0;
  // True when `contents` is a saved page of the tab tree.
  virtual bool IsSavedPage(content::WebContents* contents) = 0;
};

void AddLinkPeekHost(LinkPeekHost* host);
void RemoveLinkPeekHost(LinkPeekHost* host);

// Only http and https links are previewed.
bool IsPeekableUrl(const GURL& url);
bool CanPeekLink(content::WebContents* opener, const GURL& url);
bool PeekLink(content::WebContents* opener, const GURL& url);
// Whether automatic Peek applies to this link of a saved page; the caller
// has already checked the pref, the user gesture and the other site.
bool CanAutoPeekLink(content::WebContents* opener, const GURL& url);

// The context-menu label in the browser's language.
std::u16string PeekLinkMenuLabel();

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_LINK_PEEK_H_
