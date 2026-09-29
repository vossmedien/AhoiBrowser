// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek_input.h"

#include "base/time/time.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"

namespace ahoi::popup {
namespace {

blink::WebMouseEvent MouseDown(blink::WebPointerProperties::Button button,
                               int modifiers) {
  blink::WebMouseEvent event(
      blink::WebInputEvent::Type::kMouseDown, modifiers,
      blink::WebInputEvent::GetStaticTimeStampForTests());
  event.button = button;
  return event;
}

blink::WebKeyboardEvent KeyDown(int modifiers) {
  return blink::WebKeyboardEvent(
      blink::WebInputEvent::Type::kRawKeyDown, modifiers,
      blink::WebInputEvent::GetStaticTimeStampForTests());
}

constexpr auto kLeft = blink::WebPointerProperties::Button::kLeft;

}  // namespace

TEST(LinkPeekInputTest, PlainLeftClickAndKeyArePlain) {
  EXPECT_TRUE(IsPlainActivationInput(
      MouseDown(kLeft, blink::WebInputEvent::kNoModifiers)));
  EXPECT_TRUE(IsPlainActivationInput(KeyDown(0)));
  // Button state and lock keys are no modifiers of a link click.
  EXPECT_TRUE(IsPlainActivationInput(
      MouseDown(kLeft, blink::WebInputEvent::kLeftButtonDown |
                           blink::WebInputEvent::kCapsLockOn)));
}

TEST(LinkPeekInputTest, ModifiersAndOtherButtonsAreNotPlain) {
  for (int modifier :
       {blink::WebInputEvent::kShiftKey, blink::WebInputEvent::kControlKey,
        blink::WebInputEvent::kAltKey, blink::WebInputEvent::kMetaKey}) {
    EXPECT_FALSE(IsPlainActivationInput(MouseDown(kLeft, modifier)));
    EXPECT_FALSE(IsPlainActivationInput(KeyDown(modifier)));
  }
  EXPECT_FALSE(IsPlainActivationInput(
      MouseDown(blink::WebPointerProperties::Button::kMiddle, 0)));
}

class LinkPeekInputTrackerTest : public content::RenderViewHostTestHarness {};

TEST_F(LinkPeekInputTrackerTest, UntrackedInputIsUnknown) {
  EXPECT_FALSE(LinkPeekInputTracker::LastInputWasPlain(web_contents()));
  EXPECT_FALSE(LinkPeekInputTracker::LastInputWasPlain(nullptr));
}

TEST_F(LinkPeekInputTrackerTest, FollowsTheLastInput) {
  LinkPeekInputTracker::Track(web_contents());
  LinkPeekInputTracker::Track(web_contents());
  EXPECT_TRUE(LinkPeekInputTracker::LastInputWasPlain(web_contents()));

  LinkPeekInputTracker* const tracker =
      LinkPeekInputTracker::FromWebContents(web_contents());
  ASSERT_TRUE(tracker);
  tracker->DidGetUserInteraction(
      MouseDown(kLeft, blink::WebInputEvent::kMetaKey |
                           blink::WebInputEvent::kShiftKey));
  EXPECT_FALSE(LinkPeekInputTracker::LastInputWasPlain(web_contents()));

  tracker->DidGetUserInteraction(MouseDown(kLeft, 0));
  EXPECT_TRUE(LinkPeekInputTracker::LastInputWasPlain(web_contents()));
}

}  // namespace ahoi::popup
