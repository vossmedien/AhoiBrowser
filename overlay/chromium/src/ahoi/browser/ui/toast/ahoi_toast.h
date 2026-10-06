// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_TOAST_AHOI_TOAST_H_
#define AHOI_BROWSER_UI_TOAST_AHOI_TOAST_H_

#include <cstddef>
#include <string>

class BrowserWindowInterface;

namespace gfx {
struct VectorIcon;
}

// Short confirmations for actions that otherwise leave Ahoi's minimal chrome
// silent (user decision, 6 October 2026; Crest community request). They use
// Chromium's own toast system (one toast per window, auto-dismiss,
// accessibility announcement) with Ahoi's text and icon, so no Chromium toast
// ID is added.
namespace ahoi::toast {

enum class Event {
  kBackgroundTabOpened,
  kLinkCopied,
  kTabSaved,
  kArchived,
  kMovedToWorkspace,
  kClosed,
  kDownloadStarted,
};

// The visible text. `detail` is a Workspace or file name where the event has
// one; `count` is the number of tabs or folders an action affected.
std::u16string Message(Event event,
                       const std::u16string& detail = std::u16string(),
                       size_t count = 1);
const gfx::VectorIcon& Icon(Event event);

// Shows the confirmation in `browser`'s window. Returns false when the window
// has no toast controller (for example while it closes) or Chromium's toast
// level allows only actionable toasts.
bool Show(BrowserWindowInterface* browser,
          Event event,
          const std::u16string& detail = std::u16string(),
          size_t count = 1);

}  // namespace ahoi::toast

#endif  // AHOI_BROWSER_UI_TOAST_AHOI_TOAST_H_
