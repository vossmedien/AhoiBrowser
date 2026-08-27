// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_product_settings.h"

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {

class SyncProductSettingsTest : public testing::Test {
 protected:
  void SetUp() override { appearance::RegisterProfilePrefs(prefs_.registry()); }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(SyncProductSettingsTest, SidebarPageTintUsesBooleanAllowlistBoundary) {
  EXPECT_TRUE(
      IsPermittedProductSettingId(appearance::kSidebarPageTintEnabledPref));
  const std::optional<std::string> encoded = EncodePermittedProductSetting(
      prefs_, appearance::kSidebarPageTintEnabledPref);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ("false", *encoded);

  EXPECT_TRUE(ApplyPermittedProductSetting(
      &prefs_, appearance::kSidebarPageTintEnabledPref, "true"));
  EXPECT_TRUE(appearance::IsSidebarPageTintEnabled(prefs_));
  EXPECT_FALSE(ApplyPermittedProductSetting(
      &prefs_, appearance::kSidebarPageTintEnabledPref, "1"));
}

}  // namespace ahoi::sync
