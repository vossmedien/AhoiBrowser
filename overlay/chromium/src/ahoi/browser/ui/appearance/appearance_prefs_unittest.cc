// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_prefs.h"

#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::appearance {

class AppearancePrefsTest : public testing::Test {
 protected:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(AppearancePrefsTest, ProductDefaultsAreProfilePersistent) {
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(base::mac::MacOSMajorVersion() >= 26, IsGlassEnabled(prefs_));
#else
  EXPECT_FALSE(IsGlassEnabled(prefs_));
#endif

  const FloatingNavigationPreferences navigation =
      GetFloatingNavigationPreferences(prefs_);
  EXPECT_TRUE(navigation.auto_hide_enabled);
  EXPECT_TRUE(navigation.reveal_notch_enabled);
  EXPECT_EQ(base::Milliseconds(kDefaultFloatingNavigationAutoHideDelayMs),
            navigation.auto_hide_delay);
}

TEST_F(AppearancePrefsTest, NavigationConfigurationRoundTrips) {
  prefs_.SetBoolean(kFloatingNavigationAutoHideEnabledPref, false);
  prefs_.SetBoolean(kFloatingNavigationRevealNotchEnabledPref, false);
  prefs_.SetInteger(kFloatingNavigationAutoHideDelayMsPref, 2000);

  const FloatingNavigationPreferences navigation =
      GetFloatingNavigationPreferences(prefs_);
  EXPECT_FALSE(navigation.auto_hide_enabled);
  EXPECT_FALSE(navigation.reveal_notch_enabled);
  EXPECT_EQ(base::Seconds(2), navigation.auto_hide_delay);
}

TEST_F(AppearancePrefsTest, NavigationDelayIsClampedAtReadBoundary) {
  prefs_.SetInteger(kFloatingNavigationAutoHideDelayMsPref, -1);
  EXPECT_EQ(base::Milliseconds(kMinimumFloatingNavigationAutoHideDelayMs),
            GetFloatingNavigationPreferences(prefs_).auto_hide_delay);

  prefs_.SetInteger(kFloatingNavigationAutoHideDelayMsPref, 60000);
  EXPECT_EQ(base::Milliseconds(kMaximumFloatingNavigationAutoHideDelayMs),
            GetFloatingNavigationPreferences(prefs_).auto_hide_delay);
}

}  // namespace ahoi::appearance
