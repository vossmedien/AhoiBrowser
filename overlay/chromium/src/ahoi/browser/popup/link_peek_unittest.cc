// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek.h"

#include <memory>
#include <optional>

#include "ahoi/browser/popup/link_peek_input.h"
#include "base/time/time.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::popup {
namespace {

constexpr char kSavedPageUrl[] = "https://app.saved.example/inbox";
constexpr char kOtherSiteUrl[] = "https://news.other.example/story?id=1";

GURL SavedPage() {
  return GURL(kSavedPageUrl);
}

GURL OtherSite() {
  return GURL(kOtherSiteUrl);
}

NewWindowLink PlainBlankClick() {
  NewWindowLink link;
  link.source_url = SavedPage();
  link.target_url = OtherSite();
  link.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  link.user_gesture = true;
  link.has_opener = false;
  link.plain_activation = true;
  return link;
}

class FakeLinkPeekHost final : public LinkPeekHost {
 public:
  bool CanPeek(content::WebContents* opener, const GURL& url) override {
    return can_peek;
  }
  bool ShowPeek(content::WebContents* opener,
                const PeekRequest& request) override {
    shown = request;
    return true;
  }
  bool IsSavedPage(content::WebContents* contents) override { return saved; }

  bool can_peek = true;
  bool saved = true;
  std::optional<PeekRequest> shown;
};

}  // namespace

TEST(LinkPeekRequestTest, DefaultRequestHasNoReferrerOrInitiator) {
  const PeekRequest request(OtherSite());
  EXPECT_EQ(OtherSite(), request.url);
  EXPECT_TRUE(request.referrer.url.is_empty());
  EXPECT_FALSE(request.initiator_origin.has_value());
  EXPECT_TRUE(ui::PageTransitionTypeIncludingQualifiersIs(
      request.transition, ui::PAGE_TRANSITION_LINK));
  EXPECT_FALSE(request.started_from_context_menu);
}

TEST(LinkPeekRequestTest, TypedRequestLooksLikeTheOmnibox) {
  const PeekRequest typed = PeekRequest::ForTypedUrl(OtherSite(), false);
  EXPECT_TRUE(typed.referrer.url.is_empty());
  EXPECT_FALSE(typed.initiator_origin.has_value());
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(typed.transition,
                                           ui::PAGE_TRANSITION_TYPED));
  EXPECT_TRUE(typed.transition & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);

  const PeekRequest search = PeekRequest::ForTypedUrl(OtherSite(), true);
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(search.transition,
                                           ui::PAGE_TRANSITION_GENERATED));
  EXPECT_TRUE(search.transition & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
}

TEST(LinkPeekRequestTest, ShiftClickKeepsThePagesReferrerAndInitiator) {
  // rel=noreferrer: Blink already sent an empty referrer with kNever.
  content::OpenURLParams params(
      OtherSite(),
      content::Referrer(GURL(), network::mojom::ReferrerPolicy::kNever),
      WindowOpenDisposition::NEW_WINDOW, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/true);
  params.initiator_origin =
      url::Origin::Create(GURL("https://frame.embedded.example/"));
  const PeekRequest request = PeekRequest::FromOpenURLParams(params);
  EXPECT_EQ(OtherSite(), request.url);
  EXPECT_TRUE(request.referrer.url.is_empty());
  EXPECT_EQ(network::mojom::ReferrerPolicy::kNever, request.referrer.policy);
  EXPECT_EQ(params.initiator_origin, request.initiator_origin);
  EXPECT_TRUE(ui::PageTransitionCoreTypeIs(request.transition,
                                           ui::PAGE_TRANSITION_LINK));
}

TEST(LinkPeekSiteTest, OnlyWebLinksToAnotherSiteLeaveTheSite) {
  EXPECT_TRUE(LeavesSite(SavedPage(), OtherSite()));
  EXPECT_FALSE(
      LeavesSite(SavedPage(), GURL("https://mail.saved.example/thread/2")));
  EXPECT_FALSE(LeavesSite(SavedPage(), GURL("mailto:someone@other.example")));
  EXPECT_FALSE(LeavesSite(GURL("about:blank"), OtherSite()));
  EXPECT_FALSE(LeavesSite(GURL("chrome://settings/"), OtherSite()));
}

TEST(LinkPeekNewWindowTest, PlainBlankClickToAnotherSiteIsACandidate) {
  EXPECT_TRUE(IsAutoPeekNewWindowCandidate(PlainBlankClick()));
}

TEST(LinkPeekNewWindowTest, EverythingButThePlainClickStaysATab) {
  NewWindowLink link = PlainBlankClick();
  link.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "Cmd-click";

  link = PlainBlankClick();
  link.disposition = WindowOpenDisposition::NEW_WINDOW;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "Shift-click";

  link = PlainBlankClick();
  link.disposition = WindowOpenDisposition::NEW_POPUP;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "popup features";

  link = PlainBlankClick();
  link.plain_activation = false;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "Cmd+Shift-click";

  link = PlainBlankClick();
  link.has_opener = true;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "window.open()";

  link = PlainBlankClick();
  link.user_gesture = false;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "no gesture";

  link = PlainBlankClick();
  link.target_url = GURL("https://www.saved.example/help");
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "same site";

  // macOS browser fullscreen hands a popup over as a foreground tab.
  link = PlainBlankClick();
  link.popup_features = true;
  EXPECT_FALSE(IsAutoPeekNewWindowCandidate(link)) << "fullscreen popup";
}

class LinkPeekHostTest : public content::RenderViewHostTestHarness {
 public:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    AddLinkPeekHost(&host_);
    NavigateAndCommit(SavedPage());
    LinkPeekInputTracker::Track(web_contents());
  }

  void TearDown() override {
    RemoveLinkPeekHost(&host_);
    content::RenderViewHostTestHarness::TearDown();
  }

  std::unique_ptr<content::WebContents> MakeNewWindow() {
    return content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                             nullptr);
  }

 protected:
  FakeLinkPeekHost host_;
};

TEST_F(LinkPeekHostTest, PeekLinkHandsTheOriginalRequestToTheHost) {
  PeekRequest request(OtherSite());
  request.referrer =
      content::Referrer(GURL(), network::mojom::ReferrerPolicy::kNever);
  request.initiator_origin =
      url::Origin::Create(GURL("https://frame.embedded.example/"));
  request.started_from_context_menu = true;

  ASSERT_TRUE(PeekLink(web_contents(), request));
  ASSERT_TRUE(host_.shown.has_value());
  EXPECT_EQ(OtherSite(), host_.shown->url);
  EXPECT_EQ(network::mojom::ReferrerPolicy::kNever,
            host_.shown->referrer.policy);
  EXPECT_TRUE(host_.shown->referrer.url.is_empty());
  EXPECT_EQ(request.initiator_origin, host_.shown->initiator_origin);
  EXPECT_TRUE(host_.shown->started_from_context_menu);
}

TEST_F(LinkPeekHostTest, PeekLinkRejectsNonWebUrls) {
  EXPECT_FALSE(PeekLink(web_contents(), PeekRequest(GURL("file:///etc/"))));
  EXPECT_FALSE(host_.shown.has_value());
}

TEST_F(LinkPeekHostTest, NewWindowOfASavedPageBecomesAPeek) {
  std::unique_ptr<content::WebContents> window = MakeNewWindow();
  EXPECT_TRUE(ShouldAutoPeekNewWindow(
      web_contents(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, blink::mojom::WindowFeatures(),
      /*user_gesture=*/true));
}

TEST_F(LinkPeekHostTest, NewWindowWithPopupFeaturesStaysATab) {
  std::unique_ptr<content::WebContents> window = MakeNewWindow();
  blink::mojom::WindowFeatures features;
  features.is_popup = true;
  EXPECT_FALSE(ShouldAutoPeekNewWindow(
      web_contents(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, features,
      /*user_gesture=*/true));
  features = blink::mojom::WindowFeatures();
  features.has_width = true;
  EXPECT_FALSE(ShouldAutoPeekNewWindow(
      web_contents(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, features,
      /*user_gesture=*/true));
}

TEST_F(LinkPeekHostTest, NewWindowOfATemporaryPageStaysATab) {
  host_.saved = false;
  std::unique_ptr<content::WebContents> window = MakeNewWindow();
  EXPECT_FALSE(ShouldAutoPeekNewWindow(
      web_contents(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, blink::mojom::WindowFeatures(),
      /*user_gesture=*/true));
}

TEST_F(LinkPeekHostTest, NewWindowAfterModifierClickStaysATab) {
  blink::WebMouseEvent click(
      blink::WebInputEvent::Type::kMouseDown,
      blink::WebInputEvent::kMetaKey | blink::WebInputEvent::kShiftKey,
      base::TimeTicks::Now());
  click.button = blink::WebPointerProperties::Button::kLeft;
  LinkPeekInputTracker::FromWebContents(web_contents())
      ->DidGetUserInteraction(click);
  std::unique_ptr<content::WebContents> window = MakeNewWindow();
  EXPECT_FALSE(ShouldAutoPeekNewWindow(
      web_contents(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, blink::mojom::WindowFeatures(),
      /*user_gesture=*/true));
}

TEST_F(LinkPeekHostTest, NewWindowOfAnUntrackedPageStaysATab) {
  std::unique_ptr<content::WebContents> untracked = MakeNewWindow();
  content::WebContentsTester::For(untracked.get())
      ->NavigateAndCommit(SavedPage());
  std::unique_ptr<content::WebContents> window = MakeNewWindow();
  EXPECT_FALSE(ShouldAutoPeekNewWindow(
      untracked.get(), window.get(), OtherSite(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB, blink::mojom::WindowFeatures(),
      /*user_gesture=*/true));
}

}  // namespace ahoi::popup
