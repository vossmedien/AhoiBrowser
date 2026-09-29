// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/isolated_workspace_directory.h"

#include <set>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

MainProfileCandidate Candidate(std::string dir, int minutes, bool loaded) {
  return {.profile_dir = std::move(dir),
          .active_time = base::Time::UnixEpoch() + base::Minutes(minutes),
          .loaded = loaded};
}

TEST(IsolatedWorkspaceDirectoryTest, NeverChoosesASeparatedProfile) {
  // Chromium moved the last used directory to the separated Profile.
  EXPECT_EQ("Default",
            ChooseMainProfileDir({Candidate("Profile 1", 9, true),
                                  Candidate("Default", 1, false)},
                                 "Profile 1", {"Profile 1"}));
  EXPECT_FALSE(ChooseMainProfileDir({Candidate("Profile 1", 9, true)},
                                    "Profile 1", {"Profile 1"})
                   .has_value());
  EXPECT_FALSE(ChooseMainProfileDir({}, "Default", {}).has_value());
}

TEST(IsolatedWorkspaceDirectoryTest, PrefersLoadedThenLastUsedThenRecent) {
  const std::set<std::string> isolated = {"Profile 3"};
  EXPECT_EQ("Profile 2",
            ChooseMainProfileDir({Candidate("Default", 9, false),
                                  Candidate("Profile 2", 1, true)},
                                 "Default", isolated));
  EXPECT_EQ("Default",
            ChooseMainProfileDir({Candidate("Profile 2", 9, true),
                                  Candidate("Default", 1, true)},
                                 "Default", isolated));
  EXPECT_EQ("Profile 2",
            ChooseMainProfileDir({Candidate("Default", 1, false),
                                  Candidate("Profile 2", 5, false),
                                  Candidate("Profile 3", 9, false)},
                                 "Profile 3", isolated));
  // Equal rank keeps input order.
  EXPECT_EQ("Default",
            ChooseMainProfileDir({Candidate("Default", 1, false),
                                  Candidate("Profile 2", 1, false)},
                                 "", isolated));
}

}  // namespace
}  // namespace ahoi::session
