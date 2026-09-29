// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// WS-ISO-20: scheduling and state of a deleted separated Workspace's zone
// retirement. The CloudKit zone delete and Keychain deletes themselves are
// runtime-only (workspace_zone_retirement_mac.mm).

#include "ahoi/browser/sync/workspace_zone_retirement.h"

#include <string>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

constexpr char kWorkspace[] = "0f8b2c4e-6a1d-4e3b-9c5f-7d2a1b3c4e5f";

base::Uuid Workspace() {
  return base::Uuid::ParseLowercase(kWorkspace);
}

base::Time Start() {
  return base::Time::UnixEpoch() + base::Days(20000);
}

class WorkspaceZoneRetirementTest : public testing::Test {
 protected:
  WorkspaceZoneRetirementTest() {
    session::RegisterIsolatedProfileLocalState(local_state_.registry());
    RegisterLocalStatePrefs(local_state_.registry());
    profile_prefs_.registry()->RegisterBooleanPref(kSyncEnabledPref, false);
    profile_prefs_.registry()->RegisterStringPref(kDeviceIdPref,
                                                  std::string());
  }

  void AddSeparatedProfile() {
    ASSERT_TRUE(session::AddIsolatedProfile(
        &local_state_, {.profile_dir = "Profile 3",
                        .workspace_id = Workspace(),
                        .name = u"Work",
                        .icon = u"briefcase",
                        .state = session::IsolatedProfileState::kActive}));
  }

  TestingPrefServiceSimple local_state_;
  TestingPrefServiceSimple profile_prefs_;
};

TEST_F(WorkspaceZoneRetirementTest, WsIso20InertWithoutRegisteredPref) {
  TestingPrefServiceSimple unregistered;
  EXPECT_FALSE(
      ScheduleWorkspaceZoneRetirement(&unregistered, Workspace(), Start()));
  EXPECT_TRUE(GetDueWorkspaceZoneRetirements(&unregistered,
                                             Start() + base::Days(365))
                  .empty());
  EXPECT_FALSE(ScheduleWorkspaceZoneRetirement(nullptr, Workspace(), Start()));
  EXPECT_FALSE(
      ScheduleWorkspaceZoneRetirement(&local_state_, base::Uuid(), Start()));
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20DueOnlyAfterTombstoneRetention) {
  ASSERT_TRUE(
      ScheduleWorkspaceZoneRetirement(&local_state_, Workspace(), Start()));
  EXPECT_EQ(GetPendingWorkspaceZoneRetirements(&local_state_),
            std::vector<base::Uuid>{Workspace()});
  EXPECT_TRUE(GetDueWorkspaceZoneRetirements(
                  &local_state_, Start() + kTombstoneRetention -
                                     base::Seconds(1))
                  .empty());
  EXPECT_EQ(GetDueWorkspaceZoneRetirements(&local_state_,
                                           Start() + kTombstoneRetention),
            std::vector<base::Uuid>{Workspace()});
  // A clock moved back never makes an entry due.
  EXPECT_TRUE(GetDueWorkspaceZoneRetirements(&local_state_,
                                             Start() - base::Days(400))
                  .empty());
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20ScheduleKeepsEarliestRequest) {
  ASSERT_TRUE(
      ScheduleWorkspaceZoneRetirement(&local_state_, Workspace(), Start()));
  ASSERT_TRUE(ScheduleWorkspaceZoneRetirement(&local_state_, Workspace(),
                                              Start() + base::Days(10)));
  EXPECT_EQ(local_state_.GetList(kPendingWorkspaceZoneRetirementsPref).size(),
            1u);
  EXPECT_EQ(GetDueWorkspaceZoneRetirements(&local_state_,
                                           Start() + kTombstoneRetention)
                .size(),
            1u);
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20CompleteRemovesOnlyThatEntry) {
  const base::Uuid other = base::Uuid::GenerateRandomV4();
  ASSERT_TRUE(
      ScheduleWorkspaceZoneRetirement(&local_state_, Workspace(), Start()));
  ASSERT_TRUE(ScheduleWorkspaceZoneRetirement(&local_state_, other, Start()));
  EXPECT_TRUE(CompleteWorkspaceZoneRetirement(&local_state_, Workspace()));
  EXPECT_FALSE(CompleteWorkspaceZoneRetirement(&local_state_, Workspace()));
  EXPECT_EQ(GetPendingWorkspaceZoneRetirements(&local_state_),
            std::vector<base::Uuid>{other});
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20MalformedEntriesAreSkipped) {
  base::ListValue list;
  list.Append("not a dict");
  list.Append(base::DictValue().Set("workspace_id", "not-a-uuid"));
  list.Append(base::DictValue().Set("workspace_id", kWorkspace));
  local_state_.SetList(kPendingWorkspaceZoneRetirementsPref, std::move(list));
  EXPECT_TRUE(GetPendingWorkspaceZoneRetirements(&local_state_).empty());
  EXPECT_TRUE(GetDueWorkspaceZoneRetirements(&local_state_,
                                             Start() + base::Days(3650))
                  .empty());
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20OnlyAProfileThatSyncedSchedules) {
  EXPECT_FALSE(WorkspaceProfileMayHaveSyncedData(profile_prefs_));
  profile_prefs_.SetString(kDeviceIdPref, "id");  // Enabled once, then off.
  EXPECT_TRUE(WorkspaceProfileMayHaveSyncedData(profile_prefs_));
  profile_prefs_.SetString(kDeviceIdPref, "");
  profile_prefs_.SetBoolean(kSyncEnabledPref, true);
  EXPECT_TRUE(WorkspaceProfileMayHaveSyncedData(profile_prefs_));
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20DeletingTheProfileSchedules) {
  AddSeparatedProfile();
  profile_prefs_.SetBoolean(kSyncEnabledPref, true);
  WorkspaceZoneRetirementObserver observer(&local_state_, &profile_prefs_,
                                           "Profile 3", Workspace());
  // Unrelated registry changes schedule nothing.
  ASSERT_TRUE(session::AddIsolatedProfile(
      &local_state_,
      {.profile_dir = "Profile 4",
       .workspace_id = base::Uuid::GenerateRandomV4(),
       .name = u"Other",
       .icon = u"x"}));
  ASSERT_TRUE(session::SetIsolatedProfileState(
      &local_state_, "Profile 4", session::IsolatedProfileState::kDeleting));
  EXPECT_TRUE(GetPendingWorkspaceZoneRetirements(&local_state_).empty());

  // The `deleting` write itself records the retirement, before the deletion
  // commits Local State.
  ASSERT_TRUE(session::SetIsolatedProfileState(
      &local_state_, "Profile 3", session::IsolatedProfileState::kDeleting));
  EXPECT_EQ(GetPendingWorkspaceZoneRetirements(&local_state_),
            std::vector<base::Uuid>{Workspace()});
}

TEST_F(WorkspaceZoneRetirementTest, WsIso20NeverSyncedProfileSchedulesNothing) {
  AddSeparatedProfile();
  WorkspaceZoneRetirementObserver observer(&local_state_, &profile_prefs_,
                                           "Profile 3", Workspace());
  ASSERT_TRUE(session::SetIsolatedProfileState(
      &local_state_, "Profile 3", session::IsolatedProfileState::kDeleting));
  EXPECT_TRUE(GetPendingWorkspaceZoneRetirements(&local_state_).empty());
}

}  // namespace
}  // namespace ahoi::sync
