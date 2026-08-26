// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_service.h"

#include "base/values.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

class PrivacyModeServiceTest : public ::testing::Test {
 protected:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(PrivacyModeServiceTest, DefaultsToChromiumCompatible) {
  EXPECT_EQ(PrivacyMode::kChromiumCompatible, GetGlobalMode(prefs_));
  EXPECT_EQ(PrivacyMode::kChromiumCompatible,
            GetModeForUrl(prefs_, GURL("https://example.test/"), false));

  CookieEnforcementPolicy policy = BuildCookieEnforcementPolicy(prefs_, false);
  ASSERT_TRUE(policy.block_third_party_cookies.has_value());
  EXPECT_FALSE(*policy.block_third_party_cookies);
}

TEST_F(PrivacyModeServiceTest, UnknownFutureModePreservesCompatibility) {
  prefs_.SetString(kGlobalModePref, "future-mode");
  EXPECT_EQ(PrivacyMode::kChromiumCompatible, GetGlobalMode(prefs_));
}

TEST_F(PrivacyModeServiceTest, PersistsGlobalAndOriginModes) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kChromiumCompatible));
  EXPECT_EQ(PrivacyMode::kChromiumCompatible, GetGlobalMode(prefs_));

  const GURL origin_url("https://example.test/path");
  ASSERT_TRUE(SetOriginMode(&prefs_, origin_url, PrivacyMode::kStrict, false));
  EXPECT_EQ(PrivacyMode::kStrict,
            GetModeForUrl(prefs_, GURL("https://example.test/next"), false));
  EXPECT_EQ(PrivacyMode::kChromiumCompatible,
            GetModeForUrl(prefs_, GURL("https://other.test/"), false));

  ASSERT_TRUE(SetOriginMode(&prefs_, origin_url, std::nullopt, false));
  EXPECT_EQ(PrivacyMode::kChromiumCompatible,
            GetModeForUrl(prefs_, origin_url, false));
}

TEST_F(PrivacyModeServiceTest, RejectsNonHttpOriginAndManagedPrefs) {
  EXPECT_FALSE(SetOriginMode(&prefs_, GURL("file:///tmp/a"),
                             PrivacyMode::kChromiumCompatible, false));
  prefs_.SetManagedPref(kGlobalModePref, base::Value("chromium-compatible"));
  EXPECT_TRUE(IsGlobalModeManaged(prefs_));
  EXPECT_FALSE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
}

TEST_F(PrivacyModeServiceTest, OtrInheritsGlobalButNotExceptions) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kChromiumCompatible));
  ASSERT_TRUE(SetOriginMode(&prefs_, GURL("https://example.test/"),
                            PrivacyMode::kStrict, false));
  EXPECT_EQ(PrivacyMode::kChromiumCompatible,
            GetModeForUrl(prefs_, GURL("https://example.test/"), true));
  EXPECT_FALSE(SetOriginMode(&prefs_, GURL("https://other.test/"),
                             PrivacyMode::kStrict, true));
}

TEST_F(PrivacyModeServiceTest, RemovesOnlyKnownTrackingParameters) {
  const GURL input(
      "https://example.test/path?utm_source=news&keep=a%2Bb&gclid=123&x=1");
  EXPECT_EQ("https://example.test/path?keep=a%2Bb&x=1",
            StripKnownTrackingParameters(input).spec());
  EXPECT_EQ(
      "https://example.test/path?keep=a",
      StripKnownTrackingParameters(GURL("https://example.test/path?keep=a"))
          .spec());
}

TEST_F(PrivacyModeServiceTest, BuildsCookieSettingsPolicyByTopFrameOrigin) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
  ASSERT_TRUE(SetOriginMode(&prefs_, GURL("https://compatible.test/path"),
                            PrivacyMode::kChromiumCompatible, false));
  CookieEnforcementPolicy policy = BuildCookieEnforcementPolicy(prefs_, false);
  ASSERT_TRUE(policy.block_third_party_cookies.has_value());
  EXPECT_TRUE(*policy.block_third_party_cookies);
  ASSERT_EQ(1u, policy.top_frame_overrides.size());
  EXPECT_FALSE(policy.top_frame_overrides.at("https://compatible.test"));
}

TEST_F(PrivacyModeServiceTest,
       CookiePolicyFailsClosedInOtrAndHonorsManagement) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
  ASSERT_TRUE(SetOriginMode(&prefs_, GURL("https://compatible.test/"),
                            PrivacyMode::kChromiumCompatible, false));
  CookieEnforcementPolicy otr_policy =
      BuildCookieEnforcementPolicy(prefs_, true);
  ASSERT_TRUE(otr_policy.block_third_party_cookies.has_value());
  EXPECT_TRUE(*otr_policy.block_third_party_cookies);
  EXPECT_TRUE(otr_policy.top_frame_overrides.empty());

  prefs_.registry()->RegisterIntegerPref(prefs::kCookieControlsMode, 1);
  prefs_.SetManagedPref(prefs::kCookieControlsMode, base::Value(0));
  CookieEnforcementPolicy managed_policy =
      BuildCookieEnforcementPolicy(prefs_, false);
  EXPECT_FALSE(managed_policy.block_third_party_cookies.has_value());
  EXPECT_TRUE(managed_policy.top_frame_overrides.empty());
}

}  // namespace
}  // namespace ahoi::privacy
