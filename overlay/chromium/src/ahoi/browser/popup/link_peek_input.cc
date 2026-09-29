// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek_input.h"

#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"

namespace ahoi::popup {

bool IsPlainActivationInput(const blink::WebInputEvent& event) {
  if (event.GetModifiers() & blink::WebInputEvent::kInputModifiers) {
    return false;
  }
  // A middle click opens a background tab, but Shift+middle-click opens a
  // foreground tab; only the left button counts as plain.
  if (blink::WebInputEvent::IsMouseEventType(event.GetType())) {
    return static_cast<const blink::WebMouseEvent&>(event).button ==
           blink::WebPointerProperties::Button::kLeft;
  }
  return true;
}

LinkPeekInputTracker::LinkPeekInputTracker(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<LinkPeekInputTracker>(*contents) {}

LinkPeekInputTracker::~LinkPeekInputTracker() = default;

// static
void LinkPeekInputTracker::Track(content::WebContents* contents) {
  if (contents) {
    CreateForWebContents(contents);
  }
}

// static
bool LinkPeekInputTracker::LastInputWasPlain(content::WebContents* contents) {
  const LinkPeekInputTracker* const tracker =
      contents ? FromWebContents(contents) : nullptr;
  return tracker && tracker->last_input_plain_;
}

void LinkPeekInputTracker::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  last_input_plain_ = IsPlainActivationInput(event);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(LinkPeekInputTracker);

}  // namespace ahoi::popup
