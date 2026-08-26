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

}  // namespace
}  // namespace ahoi::developer_toolkit_prefs
