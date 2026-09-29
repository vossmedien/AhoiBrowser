// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_prefs.h"

#include <array>
#include <utility>

#include "base/test/scoped_feature_list.h"
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
  // The development gate isolates every Workspace this device learns about.
  base::test::ScopedFeatureList gate(kAhoiWorkspaceWebsiteSessions);
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

TEST_F(SessionPrefsTest, UnknownWorkspaceIsSharedWithoutTheGate) {
  const base::Uuid existing = base::Uuid::GenerateRandomV4();
  const base::Uuid arrived = base::Uuid::GenerateRandomV4();
  const std::array<base::Uuid, 1> existing_ids = {existing};
  ASSERT_TRUE(InitializeWebsiteSessionBindings(&prefs_,
                                               base::span(existing_ids)));
  const auto binding = GetOrCreateWebsiteSessionBinding(&prefs_, arrived);
  ASSERT_TRUE(binding.has_value());
  EXPECT_TRUE(binding->is_default());
  EXPECT_EQ(binding, FindWebsiteSessionBinding(&prefs_, arrived));
}

TEST_F(SessionPrefsTest, SharedLevelNeedsNoBindingState) {
  const base::Uuid workspace = base::Uuid::GenerateRandomV4();
  const std::array<base::Uuid, 1> others = {base::Uuid::GenerateRandomV4()};
  EXPECT_TRUE(BindNewWorkspaceWebsiteSessions(&prefs_, workspace,
                                              base::span(others),
                                              /*own_website_sessions=*/false));
  EXPECT_TRUE(prefs_.GetDict(kWebsiteSessionBindingsPref).empty());
  EXPECT_FALSE(ShouldUseWorkspaceWebsiteSessions(&prefs_));
}

TEST_F(SessionPrefsTest, OwnLevelAdoptsOthersAsSharedAndIsFixed) {
  const base::Uuid first = base::Uuid::GenerateRandomV4();
  const base::Uuid second = base::Uuid::GenerateRandomV4();
  const base::Uuid own = base::Uuid::GenerateRandomV4();
  const std::array<base::Uuid, 3> known = {first, second, own};
  ASSERT_TRUE(BindNewWorkspaceWebsiteSessions(&prefs_, own, base::span(known),
                                              /*own_website_sessions=*/true));
  EXPECT_TRUE(ShouldUseWorkspaceWebsiteSessions(&prefs_));
  EXPECT_TRUE(FindWebsiteSessionBinding(&prefs_, first)->is_default());
  EXPECT_TRUE(FindWebsiteSessionBinding(&prefs_, second)->is_default());
  const auto isolated = FindWebsiteSessionBinding(&prefs_, own);
  ASSERT_TRUE(isolated.has_value());
  EXPECT_FALSE(isolated->is_default());
  EXPECT_TRUE(IsKnownWebsiteSessionBinding(&prefs_, *isolated));

  // The level is chosen once; a second bind for the same Workspace fails and
  // keeps its context.
  EXPECT_FALSE(BindNewWorkspaceWebsiteSessions(&prefs_, own, base::span(known),
                                               /*own_website_sessions=*/false));
  EXPECT_EQ(isolated, FindWebsiteSessionBinding(&prefs_, own));

  // A later shared Workspace is recorded explicitly as shared.
  const base::Uuid later = base::Uuid::GenerateRandomV4();
  ASSERT_TRUE(BindNewWorkspaceWebsiteSessions(&prefs_, later,
                                              base::span(known),
                                              /*own_website_sessions=*/false));
  EXPECT_TRUE(FindWebsiteSessionBinding(&prefs_, later)->is_default());
}

TEST_F(SessionPrefsTest, OwnLevelRefusesCorruptOrManagedState) {
  const base::Uuid workspace = base::Uuid::GenerateRandomV4();
  base::DictValue future;
  future.Set("version", 2);
  prefs_.SetDict(kWebsiteSessionBindingsPref, future.Clone());
  EXPECT_FALSE(BindNewWorkspaceWebsiteSessions(&prefs_, workspace, {},
                                               /*own_website_sessions=*/true));
  EXPECT_FALSE(BindNewWorkspaceWebsiteSessions(&prefs_, workspace, {},
                                               /*own_website_sessions=*/false));
  prefs_.ClearPref(kWebsiteSessionBindingsPref);
  prefs_.SetManagedPref(kWebsiteSessionBindingsPref, base::Value(base::DictValue()));
  EXPECT_FALSE(BindNewWorkspaceWebsiteSessions(&prefs_, workspace, {},
                                               /*own_website_sessions=*/true));
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
