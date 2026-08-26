// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/privacy/privacy_mode_bubble_view.h"

#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/combobox_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class PrivacyModeBubbleViewTest : public views::ViewsTestBase {};

bool CountGlobalChange(int* callback_count, privacy::PrivacyMode) {
  ++*callback_count;
  return true;
}

bool RecordOriginChange(
    int* callback_count,
    std::optional<privacy::PrivacyMode>* last_origin_mode,
    std::optional<privacy::PrivacyMode> mode) {
  ++*callback_count;
  *last_origin_mode = mode;
  return true;
}

bool AcceptGlobalChange(privacy::PrivacyMode) {
  return true;
}

bool AcceptOriginChange(std::optional<privacy::PrivacyMode>) {
  return true;
}

TEST_F(PrivacyModeBubbleViewTest,
       SiteRepairUsesCompatibilityAndHidesAfterRepair) {
  int global_callback_count = 0;
  int origin_callback_count = 0;
  std::optional<privacy::PrivacyMode> last_origin_mode;
  auto view = std::make_unique<PrivacyModeBubbleView>(
      u"https://example.test", privacy::PrivacyMode::kStrict, std::nullopt,
      /*global_mode_managed=*/false, /*site_controls_enabled=*/true,
      /*is_off_the_record=*/false,
      base::BindRepeating(&CountGlobalChange, &global_callback_count),
      base::BindRepeating(&RecordOriginChange, &origin_callback_count,
                          &last_origin_mode));

  ASSERT_TRUE(view->repair_button_for_testing()->GetVisible());
  views::test::ButtonTestApi(view->repair_button_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(0, global_callback_count);
  EXPECT_EQ(1, origin_callback_count);
  ASSERT_TRUE(last_origin_mode);
  EXPECT_EQ(privacy::PrivacyMode::kChromiumCompatible, *last_origin_mode);
  EXPECT_FALSE(view->repair_button_for_testing()->GetVisible());
  EXPECT_EQ(2u, view->origin_mode_combobox_for_testing()->GetSelectedIndex());
}

TEST_F(PrivacyModeBubbleViewTest,
       SiteControlChangeIsPerSiteAndKeepsRepairState) {
  int origin_callback_count = 0;
  std::optional<privacy::PrivacyMode> last_origin_mode;
  auto view = std::make_unique<PrivacyModeBubbleView>(
      u"https://example.test", privacy::PrivacyMode::kChromiumCompatible,
      std::nullopt, /*global_mode_managed=*/false,
      /*site_controls_enabled=*/true, /*is_off_the_record=*/false,
      base::BindRepeating(&AcceptGlobalChange),
      base::BindRepeating(&RecordOriginChange, &origin_callback_count,
                          &last_origin_mode));

  EXPECT_FALSE(view->repair_button_for_testing()->GetVisible());
  views::test::ComboboxTestApi(view->origin_mode_combobox_for_testing())
      .PerformActionAt(1);
  EXPECT_EQ(1, origin_callback_count);
  ASSERT_TRUE(last_origin_mode);
  EXPECT_EQ(privacy::PrivacyMode::kStrict, *last_origin_mode);
  EXPECT_TRUE(view->repair_button_for_testing()->GetVisible());
}

TEST_F(PrivacyModeBubbleViewTest, UnsupportedAndIncognitoSitesCannotRepair) {
  auto unsupported = std::make_unique<PrivacyModeBubbleView>(
      u"Browser page", privacy::PrivacyMode::kStrict, std::nullopt,
      /*global_mode_managed=*/false, /*site_controls_enabled=*/false,
      /*is_off_the_record=*/false,
      base::BindRepeating(&AcceptGlobalChange),
      base::BindRepeating(&AcceptOriginChange));
  EXPECT_FALSE(unsupported->repair_button_for_testing()->GetVisible());

  auto incognito = std::make_unique<PrivacyModeBubbleView>(
      u"https://example.test", privacy::PrivacyMode::kStrict, std::nullopt,
      /*global_mode_managed=*/false, /*site_controls_enabled=*/true,
      /*is_off_the_record=*/true,
      base::BindRepeating(&AcceptGlobalChange),
      base::BindRepeating(&AcceptOriginChange));
  EXPECT_FALSE(incognito->repair_button_for_testing()->GetVisible());
}

}  // namespace
}  // namespace ahoi
