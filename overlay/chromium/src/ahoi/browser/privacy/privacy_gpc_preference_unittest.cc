// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_strict_request_rules.h"

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::privacy {
namespace {

// Handoff 070: the JS signal follows the top-level site's mode.
TEST(GpcRendererPreferenceTest, FollowsTopLevelSiteMode) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_FALSE(GlobalPrivacyControlForMainFrameUrl(
      prefs, false, GURL("https://site.test/")));  // PRIV-17 default
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));
  EXPECT_TRUE(GlobalPrivacyControlForMainFrameUrl(
      prefs, false, GURL("https://site.test/")));
  EXPECT_FALSE(GlobalPrivacyControlForMainFrameUrl(
      prefs, false, GURL("chrome://newtab/")));
}

}  // namespace
}  // namespace ahoi::privacy
