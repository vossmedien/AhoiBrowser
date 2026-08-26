// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_response_header_advanced_mode_view.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class DeveloperResponseHeaderAdvancedModeViewTest
    : public views::ViewsTestBase {};

TEST_F(DeveloperResponseHeaderAdvancedModeViewTest,
       DisabledStateClearsConsentAndReenableRequiresNewConsent) {
  auto view = std::make_unique<DeveloperResponseHeaderAdvancedModeView>(
      /*response_headers_enabled=*/false, /*acknowledged=*/true);
  EXPECT_FALSE(view->GetVisible());
  EXPECT_FALSE(view->acknowledgement_for_testing()->GetEnabled());
  EXPECT_FALSE(view->acknowledged());

  view->SetResponseHeadersEnabled(true);
  EXPECT_TRUE(view->GetVisible());
  EXPECT_TRUE(view->warning_for_testing()->GetVisible());
  EXPECT_FALSE(view->warning_for_testing()->GetText().empty());
  EXPECT_TRUE(view->acknowledgement_for_testing()->GetEnabled());
  EXPECT_FALSE(view->acknowledged());
  view->acknowledgement_for_testing()->SetChecked(true);
  EXPECT_TRUE(view->acknowledged());

  view->SetResponseHeadersEnabled(false);
  EXPECT_FALSE(view->GetVisible());
  EXPECT_FALSE(view->acknowledgement_for_testing()->GetEnabled());
  EXPECT_FALSE(view->acknowledged());
  view->SetResponseHeadersEnabled(true);
  EXPECT_FALSE(view->acknowledged());
}

TEST_F(DeveloperResponseHeaderAdvancedModeViewTest,
       ReloadRestoresExplicitConsentForEnabledRules) {
  auto view = std::make_unique<DeveloperResponseHeaderAdvancedModeView>(
      /*response_headers_enabled=*/true, /*acknowledged=*/true);
  EXPECT_TRUE(view->GetVisible());
  EXPECT_TRUE(view->acknowledgement_for_testing()->GetEnabled());
  EXPECT_TRUE(view->acknowledged());
}

}  // namespace
}  // namespace ahoi
