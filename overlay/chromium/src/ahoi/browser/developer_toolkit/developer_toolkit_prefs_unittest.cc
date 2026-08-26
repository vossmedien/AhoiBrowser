// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::developer_toolkit_prefs {
namespace {

TEST(DeveloperToolkitPrefsTest, IsMasterDisabledAndToolbarHiddenByDefault) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  EXPECT_FALSE(IsToolkitEnabled(prefs));
  EXPECT_EQ(GetToolbarVisibility(prefs), ToolbarVisibility());
  EXPECT_TRUE(prefs.GetBoolean(kShowToolkitButton));
}

TEST(DeveloperToolkitPrefsTest, ExplicitActivationAddsOneRecoverableEntry) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  EXPECT_TRUE(ActivateToolkit(prefs));
  EXPECT_TRUE(IsToolkitEnabled(prefs));
  EXPECT_EQ(GetToolbarVisibility(prefs), (ToolbarVisibility{.toolkit = true}));

  EXPECT_TRUE(SetToolbarVisibility(prefs, ToolbarVisibility()));
  EXPECT_TRUE(IsToolkitEnabled(prefs));
  EXPECT_FALSE(GetToolbarVisibility(prefs).any_visible());

  EXPECT_TRUE(ActivateToolkit(prefs));
  EXPECT_EQ(GetToolbarVisibility(prefs), (ToolbarVisibility{.toolkit = true}));
}

TEST(DeveloperToolkitPrefsTest, MasterSwitchHidesConfiguredButtons) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_TRUE(SetToolbarVisibility(
      prefs, {.cookie = true, .cache = true, .toolkit = false}));
  EXPECT_TRUE(IsToolkitEnabled(prefs));

  SetToolkitEnabled(prefs, false);
  EXPECT_FALSE(GetToolbarVisibility(prefs).any_visible());
  SetToolkitEnabled(prefs, true);
  EXPECT_EQ(GetToolbarVisibility(prefs),
            (ToolbarVisibility{.cookie = true, .cache = true}));
}

TEST(DeveloperToolkitPrefsTest,
     MasterSwitchActivationRestoresAReachableToolkitEntry) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  SetToolkitEnabled(prefs, true);
  EXPECT_TRUE(IsToolkitEnabled(prefs));
  EXPECT_EQ(GetToolbarVisibility(prefs), (ToolbarVisibility{.toolkit = true}));

  SetToolkitEnabled(prefs, false);
  EXPECT_FALSE(GetToolbarVisibility(prefs).any_visible());
  SetToolkitEnabled(prefs, true);
  EXPECT_EQ(GetToolbarVisibility(prefs), (ToolbarVisibility{.toolkit = true}));
}

TEST(DeveloperToolkitPrefsTest,
     SettingsPrivateMasterWriteRevealsPreselectedToolkitEntry) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  // chrome://settings writes the allowlisted pref directly rather than
  // calling SetToolkitEnabled(). The fresh-profile toolbar default must still
  // make that one write sufficient for activation.
  prefs.SetBoolean(kToolkitEnabled, true);
  EXPECT_TRUE(IsToolkitEnabled(prefs));
  EXPECT_EQ(GetToolbarVisibility(prefs), (ToolbarVisibility{.toolkit = true}));
}

TEST(DeveloperToolkitPrefsTest, MigratesLegacyVisibleToolbarWithoutMasterPref) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  prefs.SetBoolean(kShowCookieButton, true);
  prefs.SetBoolean(kShowCacheButton, true);
  prefs.SetBoolean(kShowToolkitButton, true);

  EXPECT_TRUE(prefs.FindPreference(kToolkitEnabled)->IsDefaultValue());
  MigrateLegacyActivation(&prefs);
  EXPECT_FALSE(prefs.FindPreference(kToolkitEnabled)->IsDefaultValue());
  EXPECT_TRUE(prefs.GetBoolean(kToolkitEnabled));
  EXPECT_TRUE(IsToolkitEnabled(prefs));
  EXPECT_EQ(
      GetToolbarVisibility(prefs),
      (ToolbarVisibility{.cookie = true, .cache = true, .toolkit = true}));

  SetToolkitEnabled(prefs, false);
  EXPECT_FALSE(prefs.FindPreference(kToolkitEnabled)->IsDefaultValue());
  EXPECT_FALSE(IsToolkitEnabled(prefs));
  EXPECT_FALSE(GetToolbarVisibility(prefs).any_visible());
}

TEST(DeveloperToolkitPrefsTest,
     LegacyMigrationPreservesExplicitMasterAndFreshDefaults) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  MigrateLegacyActivation(&prefs);
  EXPECT_TRUE(prefs.FindPreference(kToolkitEnabled)->IsDefaultValue());
  EXPECT_FALSE(IsToolkitEnabled(prefs));

  prefs.SetBoolean(kToolkitEnabled, false);
  prefs.SetBoolean(kShowCookieButton, true);
  MigrateLegacyActivation(&prefs);
  EXPECT_FALSE(prefs.GetBoolean(kToolkitEnabled));
}

TEST(DeveloperToolkitPrefsTest, LegacyFalseToolbarChoicesDoNotActivateToolkit) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());

  prefs.SetBoolean(kShowCookieButton, false);
  prefs.SetBoolean(kShowCacheButton, false);
  prefs.SetBoolean(kShowToolkitButton, false);

  EXPECT_FALSE(IsToolkitEnabled(prefs));
  EXPECT_FALSE(GetToolbarVisibility(prefs).any_visible());
}

}  // namespace
}  // namespace ahoi::developer_toolkit_prefs
