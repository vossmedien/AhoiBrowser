// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include <array>
#include <utility>

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

TEST_F(SessionPrefsTest, ExistingWorkspacesStayDefaultAndNewOnesAreLocal) {
  const base::Uuid existing = base::Uuid::ParseLowercase(
      "10000000-0000-4000-8000-000000000001");
  const base::Uuid added = base::Uuid::ParseLowercase(
      "10000000-0000-4000-8000-000000000002");
  const std::array<base::Uuid, 1> existing_ids = {existing};
  EXPECT_FALSE(
      GetOrCreateWebsiteSessionBinding(&prefs_, existing).has_value());
  ASSERT_TRUE(InitializeWebsiteSessionBindings(&prefs_,
                                               base::span(existing_ids)));
  const auto original = GetOrCreateWebsiteSessionBinding(&prefs_, existing);
  ASSERT_TRUE(original.has_value());
  EXPECT_TRUE(original->is_default());

  const auto isolated = GetOrCreateWebsiteSessionBinding(&prefs_, added);
  ASSERT_TRUE(isolated.has_value());
  EXPECT_FALSE(isolated->is_default());
  EXPECT_EQ(isolated, GetOrCreateWebsiteSessionBinding(&prefs_, added));
  EXPECT_TRUE(IsKnownWebsiteSessionBinding(&prefs_, *isolated));
  const auto recovery = GetWebsiteSessionRecoveryBinding(&prefs_);
  ASSERT_TRUE(recovery.has_value());
  EXPECT_FALSE(recovery->is_default());
  EXPECT_NE(recovery, isolated);
  EXPECT_TRUE(IsKnownWebsiteSessionBinding(&prefs_, *recovery));

  const std::array<base::Uuid, 2> later_snapshot = {existing, added};
  EXPECT_TRUE(InitializeWebsiteSessionBindings(&prefs_,
                                               base::span(later_snapshot)));
  EXPECT_EQ(isolated, GetOrCreateWebsiteSessionBinding(&prefs_, added));
  EXPECT_TRUE(GetOrCreateWebsiteSessionBinding(&prefs_, existing)->is_default());
}

TEST_F(SessionPrefsTest, CorruptBindingDoesNotFallBackToDefault) {
  const base::Uuid workspace = base::Uuid::ParseLowercase(
      "10000000-0000-4000-8000-000000000001");
  const std::array<base::Uuid, 1> existing_ids = {workspace};
  base::DictValue future;
  future.Set("version", 2);
  prefs_.SetDict(kWebsiteSessionBindingsPref, std::move(future));
  EXPECT_FALSE(InitializeWebsiteSessionBindings(&prefs_,
                                                base::span(existing_ids)));
  EXPECT_FALSE(
      GetOrCreateWebsiteSessionBinding(&prefs_, workspace).has_value());
  EXPECT_FALSE(GetWebsiteSessionRecoveryBinding(&prefs_).has_value());
  EXPECT_FALSE(IsKnownWebsiteSessionBinding(
      &prefs_, WebsiteSessionBinding{.context_id =
                                         base::Uuid::GenerateRandomV4()}));
}

}  // namespace ahoi::session
