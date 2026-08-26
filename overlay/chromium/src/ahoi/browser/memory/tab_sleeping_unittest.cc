// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/memory/tab_sleeping.h"

#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::memory {
namespace {

TEST(TabSleepingPolicyTest, CriticalInputsRemainProtected) {
  SleepEligibilityInputs inputs;
  inputs.active = true;
  EXPECT_FALSE(IsManualSleepAllowed(inputs));
  EXPECT_EQ(SleepBlockReason::kActiveTab, GetBlockedReason(inputs));

  inputs = {};
  inputs.picture_in_picture = true;
  EXPECT_FALSE(IsManualSleepAllowed(inputs));
  EXPECT_EQ(SleepBlockReason::kPictureInPicture, GetBlockedReason(inputs));

  inputs = {};
  inputs.capturing = true;
  EXPECT_FALSE(IsManualSleepAllowed(inputs));
  EXPECT_EQ(SleepBlockReason::kCapture, GetBlockedReason(inputs));

  inputs = {};
  inputs.form_state = true;
  EXPECT_FALSE(IsManualSleepAllowed(inputs));
  EXPECT_EQ(SleepBlockReason::kFormState, GetBlockedReason(inputs));
}

TEST(TabSleepingPolicyTest, OrdinaryBackgroundTabIsEligible) {
  EXPECT_TRUE(IsManualSleepAllowed(SleepEligibilityInputs()));
  EXPECT_EQ(SleepBlockReason::kNone,
            GetBlockedReason(SleepEligibilityInputs()));
}

TEST(TabSleepingPolicyTest, NeverSleepExceptionRoundTrips) {
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterDictionaryPref(
      performance_manager::user_tuning::prefs::
          kTabDiscardingExceptionsWithTime);
  const GURL url("https://example.test:8443/project/page");

  EXPECT_EQ("https://example.test:8443/*", GetNeverSleepKey(url));
  EXPECT_FALSE(IsNeverSleepForUrl(&prefs, url));
  EXPECT_TRUE(SetNeverSleepForUrl(&prefs, url, true));
  EXPECT_TRUE(IsNeverSleepForUrl(&prefs, url));

  // Other policy entries must survive when one origin is toggled off.
  SetNeverSleepForUrl(&prefs, GURL("https://other.test/page"), true);
  EXPECT_TRUE(SetNeverSleepForUrl(&prefs, url, false));
  EXPECT_FALSE(IsNeverSleepForUrl(&prefs, url));
  EXPECT_TRUE(IsNeverSleepForUrl(&prefs, GURL("https://other.test/page")));
}

}  // namespace
}  // namespace ahoi::memory
