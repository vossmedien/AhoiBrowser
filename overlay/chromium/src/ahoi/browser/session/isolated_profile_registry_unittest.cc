// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/isolated_profile_registry.h"

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

class IsolatedProfileRegistryTest : public ::testing::Test {
 public:
  void SetUp() override {
    RegisterIsolatedProfileLocalState(local_state_.registry());
  }

 protected:
  static IsolatedProfileEntry Entry(std::string dir) {
    return {.profile_dir = std::move(dir),
            .workspace_id = base::Uuid::GenerateRandomV4(),
            .name = u"Kunde",
            .icon = u"K",
            .accent_argb = 0xFF4F8DE8u};
  }

  TestingPrefServiceSimple local_state_;
};

TEST_F(IsolatedProfileRegistryTest, RoundTripsEntriesInOrder) {
  const IsolatedProfileEntry first = Entry("Profile 1");
  IsolatedProfileEntry second = Entry("Profile 2");
  second.accent_argb.reset();
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, first));
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, second));
  EXPECT_EQ((std::vector<IsolatedProfileEntry>{first, second}),
            GetIsolatedProfiles(&local_state_));
  EXPECT_EQ(second, FindIsolatedProfile(&local_state_, "Profile 2"));
  EXPECT_FALSE(FindIsolatedProfile(&local_state_, "Default").has_value());
}

TEST_F(IsolatedProfileRegistryTest, RejectsDuplicatesAndInvalidEntries) {
  IsolatedProfileEntry entry = Entry("Profile 1");
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, entry));
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, entry));
  IsolatedProfileEntry same_workspace = Entry("Profile 2");
  same_workspace.workspace_id = entry.workspace_id;
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, same_workspace));
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, Entry("../Default")));
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, Entry("")));
  IsolatedProfileEntry unnamed = Entry("Profile 3");
  unnamed.name.clear();
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, unnamed));
  EXPECT_EQ(1u, GetIsolatedProfiles(&local_state_).size());
}

TEST_F(IsolatedProfileRegistryTest, TracksStateAndRemoves) {
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, Entry("Profile 1")));
  EXPECT_EQ(IsolatedProfileState::kCreating,
            FindIsolatedProfile(&local_state_, "Profile 1")->state);
  ASSERT_TRUE(SetIsolatedProfileState(&local_state_, "Profile 1",
                                      IsolatedProfileState::kActive));
  EXPECT_EQ(IsolatedProfileState::kActive,
            FindIsolatedProfile(&local_state_, "Profile 1")->state);
  EXPECT_FALSE(SetIsolatedProfileState(&local_state_, "Profile 9",
                                       IsolatedProfileState::kActive));
  EXPECT_TRUE(RemoveIsolatedProfile(&local_state_, "Profile 1"));
  EXPECT_FALSE(RemoveIsolatedProfile(&local_state_, "Profile 1"));
  EXPECT_TRUE(GetIsolatedProfiles(&local_state_).empty());
}

TEST_F(IsolatedProfileRegistryTest, SweepRemovesProfilesThatNoLongerExist) {
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, Entry("Profile 1")));
  ASSERT_TRUE(AddIsolatedProfile(&local_state_, Entry("Profile 2")));
  EXPECT_EQ(std::vector<std::string>{"Profile 1"},
            RemoveIsolatedProfilesNotIn(&local_state_,
                                        {"Default", "Profile 2"}));
  ASSERT_EQ(1u, GetIsolatedProfiles(&local_state_).size());
  EXPECT_EQ("Profile 2", GetIsolatedProfiles(&local_state_)[0].profile_dir);
}

TEST_F(IsolatedProfileRegistryTest, SkipsMalformedStoredEntries) {
  base::ListValue list;
  list.Append("not a dict");
  base::DictValue bad_uuid;
  bad_uuid.Set("profile_dir", "Profile 1");
  bad_uuid.Set("workspace_id", "nope");
  bad_uuid.Set("name", "x");
  bad_uuid.Set("icon", "x");
  bad_uuid.Set("state", 1);
  list.Append(std::move(bad_uuid));
  local_state_.SetList(kIsolatedProfilesPref, std::move(list));
  EXPECT_TRUE(GetIsolatedProfiles(&local_state_).empty());
  // A new valid entry is still accepted next to damaged ones.
  EXPECT_TRUE(AddIsolatedProfile(&local_state_, Entry("Profile 2")));
  EXPECT_EQ(1u, GetIsolatedProfiles(&local_state_).size());
}

TEST_F(IsolatedProfileRegistryTest, ManagedListIsNotWritten) {
  local_state_.SetManagedPref(kIsolatedProfilesPref,
                              base::Value(base::ListValue()));
  EXPECT_FALSE(AddIsolatedProfile(&local_state_, Entry("Profile 1")));
}

}  // namespace
}  // namespace ahoi::session
