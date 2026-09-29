// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_LINK_PEEK_INPUT_H_
#define AHOI_BROWSER_POPUP_LINK_PEEK_INPUT_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace blink {
class WebInputEvent;
}  // namespace blink

namespace ahoi::popup {

// True when `event` can activate a link without a modifier: the left mouse
// button, a touch or a key, with no Shift, Ctrl, Alt or Cmd held.
bool IsPlainActivationInput(const blink::WebInputEvent& event);

// Remembers whether a page's last user input was plain. Chromium reports a
// Cmd+Shift-click on a target=_blank link and a plain click on it with the
// same foreground-tab disposition; only this input tells them apart, so
// automatic Peek never takes a tab the user asked for on purpose.
class LinkPeekInputTracker final
    : public content::WebContentsObserver,
      public content::WebContentsUserData<LinkPeekInputTracker> {
 public:
  LinkPeekInputTracker(const LinkPeekInputTracker&) = delete;
  LinkPeekInputTracker& operator=(const LinkPeekInputTracker&) = delete;
  ~LinkPeekInputTracker() override;

  // Starts tracking `contents`; repeated calls keep the first tracker.
  static void Track(content::WebContents* contents);
  // False when `contents` is not tracked, so its input is unknown.
  static bool LastInputWasPlain(content::WebContents* contents);

  // content::WebContentsObserver:
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;

 private:
  friend class content::WebContentsUserData<LinkPeekInputTracker>;

  explicit LinkPeekInputTracker(content::WebContents* contents);

  bool last_input_plain_ = true;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_LINK_PEEK_INPUT_H_
