// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include "base/values.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {

class SessionPrefsTest : public ::testing::Test {
 public:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(SessionPrefsTest, DefaultsToAsk) {
  EXPECT_EQ(StartupMode::kAsk, GetStartupMode(prefs_));
  EXPECT_EQ("ask", prefs_.GetString(kStartupModePref));
}

TEST_F(SessionPrefsTest, PersistsEverySupportedMode) {
  EXPECT_TRUE(SetStartupMode(&prefs_, StartupMode::kContinue));
  EXPECT_EQ(StartupMode::kContinue, GetStartupMode(prefs_));
  EXPECT_EQ("continue", prefs_.GetString(kStartupModePref));

  EXPECT_TRUE(SetStartupMode(&prefs_, StartupMode::kEmpty));
  EXPECT_EQ(StartupMode::kEmpty, GetStartupMode(prefs_));
  EXPECT_EQ("empty", prefs_.GetString(kStartupModePref));

  EXPECT_TRUE(SetStartupMode(&prefs_, StartupMode::kAsk));
  EXPECT_EQ(StartupMode::kAsk, GetStartupMode(prefs_));
}

TEST_F(SessionPrefsTest, InvalidStoredValueFallsBackToAsk) {
  prefs_.SetString(kStartupModePref, "future-or-corrupt");
  EXPECT_EQ(StartupMode::kAsk, GetStartupMode(prefs_));
  EXPECT_FALSE(StartupModeFromPrefValue("future-or-corrupt").has_value());
}

TEST_F(SessionPrefsTest, ManagedValueCannotBeOverwritten) {
  prefs_.SetManagedPref(kStartupModePref, base::Value("continue"));
  EXPECT_TRUE(IsStartupModeManaged(prefs_));
  EXPECT_EQ(StartupMode::kContinue, GetStartupMode(prefs_));
  EXPECT_FALSE(SetStartupMode(&prefs_, StartupMode::kEmpty));
  EXPECT_EQ(StartupMode::kContinue, GetStartupMode(prefs_));
}

TEST_F(SessionPrefsTest, InvalidEnumIsRejected) {
  EXPECT_FALSE(SetStartupMode(&prefs_, static_cast<StartupMode>(99)));
  EXPECT_EQ(StartupMode::kAsk, GetStartupMode(prefs_));
}

}  // namespace ahoi::session
