// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_directory_order.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

DirectoryWorkspace Main(std::string sort_key) {
  return {.workspace_id = base::Uuid::GenerateRandomV4(),
          .sort_key = std::move(sort_key)};
}

IsolatedProfileEntry Isolated(std::string dir, std::string sort_key) {
  return {.profile_dir = std::move(dir),
          .workspace_id = base::Uuid::GenerateRandomV4(),
          .name = u"Kunde",
          .icon = u"K",
          .state = IsolatedProfileState::kActive,
          .sort_key = std::move(sort_key)};
}

DirectoryWorkspace AsDirectory(const IsolatedProfileEntry& entry) {
  return {.workspace_id = entry.workspace_id,
          .sort_key = entry.sort_key,
          .profile_dir = entry.profile_dir};
}

// A separated Workspace created between two main ones stays between them.
TEST(WorkspaceDirectoryOrderTest, MergesByKeyAcrossProfiles) {
  const DirectoryWorkspace inbox = Main("0");
  const DirectoryWorkspace work = Main("00000000");
  const DirectoryWorkspace later = Main("00000000@@");
  const IsolatedProfileEntry client = Isolated("Profile 1", "00000000@");
  EXPECT_EQ((std::vector<DirectoryWorkspace>{inbox, work, AsDirectory(client),
                                             later}),
            OrderDirectoryWorkspaces({inbox, work, later}, {client}));
}

TEST(WorkspaceDirectoryOrderTest, EqualKeysPutMainFirst) {
  const DirectoryWorkspace main = Main("00000000@");
  const IsolatedProfileEntry client = Isolated("Profile 1", "00000000@");
  EXPECT_EQ((std::vector<DirectoryWorkspace>{main, AsDirectory(client)}),
            OrderDirectoryWorkspaces({main}, {client}));
}

// Entries from before step 2 keep today's position after everything else,
// in registry order; entries being deleted are not listed.
TEST(WorkspaceDirectoryOrderTest, UnkeyedFollowAndDeletingIsSkipped) {
  const DirectoryWorkspace main = Main("00000000");
  const IsolatedProfileEntry old_b = Isolated("Profile 2", "");
  const IsolatedProfileEntry old_a = Isolated("Profile 1", "");
  IsolatedProfileEntry deleting = Isolated("Profile 3", "0");
  deleting.state = IsolatedProfileState::kDeleting;
  IsolatedProfileEntry converting = Isolated("Profile 5", "0");
  converting.state = IsolatedProfileState::kConverting;
  const IsolatedProfileEntry keyed = Isolated("Profile 4", "00000000@");
  EXPECT_EQ((std::vector<DirectoryWorkspace>{main, AsDirectory(keyed),
                                             AsDirectory(old_b),
                                             AsDirectory(old_a)}),
            OrderDirectoryWorkspaces({main}, {old_b, old_a, deleting,
                                              converting, keyed}));
}

TEST(WorkspaceDirectoryOrderTest, NextKeyFollowsEveryWorkspace) {
  EXPECT_EQ("00000000", NextDirectorySortKey({}));
  EXPECT_EQ("00000000", NextDirectorySortKey({Main("")}));
  const IsolatedProfileEntry client = Isolated("Profile 1", "00000000@@");
  EXPECT_EQ("00000000@@@",
            NextDirectorySortKey(OrderDirectoryWorkspaces(
                {Main("0"), Main("00000000@")}, {client})));
}

}  // namespace
}  // namespace ahoi::session
