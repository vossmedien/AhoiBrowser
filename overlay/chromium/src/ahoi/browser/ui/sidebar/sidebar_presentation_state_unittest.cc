// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"

#include "base/values.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {

class SidebarPresentationStateTest : public testing::Test {
 protected:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(SidebarPresentationStateTest, DefaultsToDocked) {
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));
  EXPECT_EQ(SidebarPresentationMode::kDocked,
            GetVisibleModeBeforeHidden(prefs_));
}

TEST_F(SidebarPresentationStateTest, FloatingRoundTrips) {
  EXPECT_TRUE(SetPresentationMode(&prefs_, SidebarPresentationMode::kFloating));
  EXPECT_EQ(SidebarPresentationMode::kFloating, GetPresentationMode(prefs_));

  EXPECT_TRUE(SetPresentationMode(&prefs_, SidebarPresentationMode::kDocked));
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));
}

TEST_F(SidebarPresentationStateTest, HiddenRestoresPreviousVisibleMode) {
  EXPECT_TRUE(SetPresentationMode(&prefs_, SidebarPresentationMode::kFloating));
  EXPECT_TRUE(SetPresentationMode(&prefs_, SidebarPresentationMode::kHidden));
  EXPECT_EQ(SidebarPresentationMode::kHidden, GetPresentationMode(prefs_));
  EXPECT_EQ(SidebarPresentationMode::kFloating,
            GetVisibleModeBeforeHidden(prefs_));

  EXPECT_TRUE(SetPresentationMode(&prefs_, GetVisibleModeBeforeHidden(prefs_)));
  EXPECT_EQ(SidebarPresentationMode::kFloating, GetPresentationMode(prefs_));
}

TEST_F(SidebarPresentationStateTest, InvalidStoredValueFallsBackToDocked) {
  prefs_.SetInteger(kSidebarPresentationModePref, 99);
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));
  EXPECT_FALSE(
      SetPresentationMode(&prefs_, static_cast<SidebarPresentationMode>(99)));
}

TEST_F(SidebarPresentationStateTest, MiniPlayerExpansionRoundTrips) {
  EXPECT_FALSE(IsMiniPlayerExpanded(prefs_));
  EXPECT_TRUE(SetMiniPlayerExpanded(&prefs_, true));
  EXPECT_TRUE(IsMiniPlayerExpanded(prefs_));
  EXPECT_TRUE(SetMiniPlayerExpanded(&prefs_, false));
  EXPECT_FALSE(IsMiniPlayerExpanded(prefs_));
}

// WS-ISO-04: the target Profile takes the source window's presentation,
// including where a hidden sidebar comes back to.
TEST_F(SidebarPresentationStateTest, AdoptPresentationCarriesHiddenState) {
  EXPECT_TRUE(AdoptPresentation(&prefs_, SidebarPresentationMode::kHidden,
                                SidebarPresentationMode::kFloating));
  EXPECT_EQ(SidebarPresentationMode::kHidden, GetPresentationMode(prefs_));
  EXPECT_EQ(SidebarPresentationMode::kFloating,
            GetVisibleModeBeforeHidden(prefs_));

  EXPECT_TRUE(AdoptPresentation(&prefs_, SidebarPresentationMode::kDocked,
                                SidebarPresentationMode::kHidden));
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));
  // A hidden restore target is never stored; docked stands in for it.
  EXPECT_EQ(SidebarPresentationMode::kDocked,
            GetVisibleModeBeforeHidden(prefs_));
}

TEST_F(SidebarPresentationStateTest, AdoptPresentationRejectsInvalidInput) {
  EXPECT_FALSE(AdoptPresentation(nullptr, SidebarPresentationMode::kFloating,
                                 SidebarPresentationMode::kDocked));
  EXPECT_FALSE(AdoptPresentation(&prefs_,
                                 static_cast<SidebarPresentationMode>(99),
                                 SidebarPresentationMode::kDocked));
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));

  prefs_.SetManagedPref(kSidebarPresentationModePref,
                        base::Value(static_cast<int>(
                            SidebarPresentationMode::kDocked)));
  EXPECT_FALSE(AdoptPresentation(&prefs_, SidebarPresentationMode::kHidden,
                                 SidebarPresentationMode::kDocked));
  EXPECT_EQ(SidebarPresentationMode::kDocked, GetPresentationMode(prefs_));
}

TEST(SidebarPresentationLayoutTest, DockedReservesViewport) {
  const SidebarLayoutPolicy policy =
      GetLayoutPolicy(SidebarPresentationMode::kDocked);
  EXPECT_TRUE(policy.visible);
  EXPECT_TRUE(policy.reserve_viewport);
  EXPECT_FALSE(policy.overlays_web_contents);
}

TEST(SidebarPresentationLayoutTest, FloatingOverlaysWithoutReflow) {
  const SidebarLayoutPolicy policy =
      GetLayoutPolicy(SidebarPresentationMode::kFloating);
  EXPECT_TRUE(policy.visible);
  EXPECT_FALSE(policy.reserve_viewport);
  EXPECT_TRUE(policy.overlays_web_contents);
}

TEST(SidebarPresentationLayoutTest, HiddenReleasesViewport) {
  const SidebarLayoutPolicy policy =
      GetLayoutPolicy(SidebarPresentationMode::kHidden);
  EXPECT_FALSE(policy.visible);
  EXPECT_FALSE(policy.reserve_viewport);
  EXPECT_FALSE(policy.overlays_web_contents);
}

}  // namespace ahoi::sidebar
