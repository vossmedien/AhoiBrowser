// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/popup_types.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

namespace ahoi::popup {

TEST(AhoiPopupTypesTest, OnlyUserPopupDispositionIsEligible) {
  blink::mojom::WindowFeatures features;
  EXPECT_EQ(PopupSafety::kNotAWebPopup,
            ClassifyPopupForOverlay(GURL("https://example.test/"),
                                    WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                    features));
  EXPECT_EQ(
      PopupSafety::kSafeForOverlay,
      ClassifyPopupForOverlay(GURL("https://example.test/"),
                              WindowOpenDisposition::NEW_POPUP, features));
}

TEST(AhoiPopupTypesTest, AllowsAboutBlankBootstrapPopup) {
  blink::mojom::WindowFeatures features;
  EXPECT_EQ(
      PopupSafety::kSafeForOverlay,
      ClassifyPopupForOverlay(GURL("about:blank"),
                              WindowOpenDisposition::NEW_POPUP, features));
}

TEST(AhoiPopupTypesTest, KeepsUnsupportedSchemesOnNativePath) {
  blink::mojom::WindowFeatures features;
  EXPECT_EQ(
      PopupSafety::kInvalidOrUnsupportedScheme,
      ClassifyPopupForOverlay(GURL("chrome://settings/"),
                              WindowOpenDisposition::NEW_POPUP, features));
  EXPECT_EQ(
      PopupSafety::kInvalidOrUnsupportedScheme,
      ClassifyPopupForOverlay(GURL("data:text/html,popup"),
                              WindowOpenDisposition::NEW_POPUP, features));
}

TEST(AhoiPopupTypesTest, KeepsSensitiveFlowsOnNativePath) {
  blink::mojom::WindowFeatures features;
  EXPECT_EQ(
      PopupSafety::kSensitiveAuthenticationFlow,
      ClassifyPopupForOverlay(GURL("https://id.example.test/oauth/authorize"),
                              WindowOpenDisposition::NEW_POPUP, features));
  EXPECT_EQ(
      PopupSafety::kPaymentFlow,
      ClassifyPopupForOverlay(GURL("https://shop.example.test/checkout"),
                              WindowOpenDisposition::NEW_POPUP, features));
  EXPECT_EQ(
      PopupSafety::kPasskeyFlow,
      ClassifyPopupForOverlay(GURL("https://id.example.test/webauthn"),
                              WindowOpenDisposition::NEW_POPUP, features));
}

TEST(AhoiPopupTypesTest, RejectsFifthPaneBeforeTransfer) {
  EXPECT_EQ(PopupSplitAvailability::kAvailable,
            ClassifySplitAvailability(true, true, 3u, 4u));
  EXPECT_EQ(PopupSplitAvailability::kSplitFull,
            ClassifySplitAvailability(true, true, 4u, 4u));
  EXPECT_EQ(PopupSplitAvailability::kOpenerMissing,
            ClassifySplitAvailability(false, false, 0u, 4u));
  EXPECT_EQ(PopupSplitAvailability::kOpenerNotInWindow,
            ClassifySplitAvailability(true, false, 0u, 4u));
}

}  // namespace ahoi::popup
