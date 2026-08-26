// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/popup_overlay_service.h"

#include <memory>

#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::popup {

class PopupOverlayServiceTest : public content::RenderViewHostTestHarness,
                                public PopupOverlayService::Observer {
 public:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    service_ = std::make_unique<PopupOverlayService>(this);
  }

  void TearDown() override {
    service_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  std::unique_ptr<content::WebContents> MakePopup() {
    return content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                             nullptr);
  }

  void OnPopupServiceStateChanged() override { ++state_change_count_; }
  void OnPopupServiceWillDetach() override { ++will_detach_count_; }
  void OnPopupServiceFallbackRequested(PopupFallbackReason reason) override {
    ++fallback_count_;
    last_fallback_reason_ = reason;
  }

 protected:
  std::unique_ptr<PopupOverlayService> service_;
  int state_change_count_ = 0;
  int will_detach_count_ = 0;
  int fallback_count_ = 0;
  PopupFallbackReason last_fallback_reason_ =
      PopupFallbackReason::kUnsupportedNavigation;
};

TEST_F(PopupOverlayServiceTest, TransfersAndRestoresSameWebContents) {
  std::unique_ptr<content::WebContents> popup = MakePopup();
  content::WebContents* const identity = popup.get();
  ASSERT_TRUE(service_->Adopt(web_contents(), &popup));
  EXPECT_FALSE(popup);
  EXPECT_TRUE(service_->OwnsContents(identity));

  std::unique_ptr<content::WebContents> transferred =
      service_->ReleaseForTransfer();
  ASSERT_EQ(identity, transferred.get());
  EXPECT_FALSE(service_->IsShowing());
  EXPECT_EQ(1, will_detach_count_);

  ASSERT_TRUE(
      service_->RestoreAfterRejectedTransfer(web_contents(), &transferred));
  EXPECT_FALSE(transferred);
  EXPECT_EQ(identity, service_->popup_contents());
  EXPECT_EQ(2, state_change_count_);
}

TEST_F(PopupOverlayServiceTest, FailedAdoptionDoesNotConsumeWebContents) {
  std::unique_ptr<content::WebContents> popup = MakePopup();
  content::WebContents* const identity = popup.get();

  EXPECT_FALSE(service_->Adopt(nullptr, &popup));
  EXPECT_EQ(identity, popup.get());
  EXPECT_FALSE(service_->IsShowing());
}

TEST_F(PopupOverlayServiceTest, RejectedRestoreSurvivesMissingOpener) {
  std::unique_ptr<content::WebContents> popup = MakePopup();
  content::WebContents* const identity = popup.get();

  EXPECT_TRUE(service_->RestoreAfterRejectedTransfer(nullptr, &popup));
  EXPECT_FALSE(popup);
  EXPECT_EQ(identity, service_->popup_contents());
  EXPECT_EQ(nullptr, service_->opener());
}

TEST_F(PopupOverlayServiceTest, FailedRestoreDoesNotConsumeWebContents) {
  std::unique_ptr<content::WebContents> first_popup = MakePopup();
  ASSERT_TRUE(service_->Adopt(web_contents(), &first_popup));
  std::unique_ptr<content::WebContents> second_popup = MakePopup();
  content::WebContents* const identity = second_popup.get();

  EXPECT_FALSE(
      service_->RestoreAfterRejectedTransfer(web_contents(), &second_popup));
  EXPECT_EQ(identity, second_popup.get());
}

TEST_F(PopupOverlayServiceTest, CoalescesNativeFallbackRequests) {
  std::unique_ptr<content::WebContents> popup = MakePopup();
  ASSERT_TRUE(service_->Adopt(web_contents(), &popup));
  service_->RequestNativeFallback(
      PopupFallbackReason::kSensitiveAuthenticationFlow);
  service_->RequestNativeFallback(PopupFallbackReason::kPaymentFlow);

  EXPECT_EQ(1, fallback_count_);
  EXPECT_EQ(PopupFallbackReason::kSensitiveAuthenticationFlow,
            last_fallback_reason_);
}

TEST_F(PopupOverlayServiceTest, OwnedCloseCleansUpWithoutPhantomContents) {
  std::unique_ptr<content::WebContents> owned_popup = MakePopup();
  ASSERT_TRUE(service_->Adopt(web_contents(), &owned_popup));
  content::WebContents* const popup = service_->popup_contents();
  EXPECT_TRUE(service_->CloseOwnedContents(popup));
  EXPECT_FALSE(service_->IsShowing());
  EXPECT_EQ(1, will_detach_count_);
}

}  // namespace ahoi::popup
