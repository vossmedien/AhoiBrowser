// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/other_profile_media.h"

#include <vector>

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sidebar {
namespace {

// WS-ISO-18: only the Workspace whose page plays is marked in the main
// Profile; a separated Profile's one Workspace takes every page.
TEST(OtherProfileMediaTest, MainProfileMarksOnlyThePlayingWorkspace) {
  const base::Uuid playing = base::Uuid::GenerateRandomV4();
  const base::Uuid quiet = base::Uuid::GenerateRandomV4();
  const std::vector<PageAudioState> pages = {
      {.workspace_id = playing, .audible = true},
      {.workspace_id = quiet, .audible = false},
      {.workspace_id = std::nullopt, .audible = true},
  };
  EXPECT_TRUE(SwitcherEntryPlaysAudio(pages, false, playing));
  EXPECT_FALSE(SwitcherEntryPlaysAudio(pages, false, quiet));
}

TEST(OtherProfileMediaTest, SeparatedProfileCountsEveryAudiblePage) {
  const base::Uuid workspace = base::Uuid::GenerateRandomV4();
  const std::vector<PageAudioState> untracked = {
      {.workspace_id = std::nullopt, .audible = true}};
  EXPECT_TRUE(SwitcherEntryPlaysAudio(untracked, true, workspace));
  const std::vector<PageAudioState> silent = {
      {.workspace_id = workspace, .audible = false}};
  EXPECT_FALSE(SwitcherEntryPlaysAudio(silent, true, workspace));
  EXPECT_FALSE(SwitcherEntryPlaysAudio({}, true, workspace));
}

TEST(OtherProfileMediaTest, LabelsNameTheWorkspace) {
  EXPECT_EQ(u"Getrennt – Vollständig getrennt · spielt Audio",
            AudibleSwitcherTitle(
                u"Getrennt – Vollständig getrennt", true));
  EXPECT_EQ(u"Inbox · playing audio",
            AudibleSwitcherTitle(u"Inbox", false));
  EXPECT_EQ(u"Wiedergabe in „Inbox“ pausieren",
            PauseOtherProfileMediaLabel(u"Inbox", true));
  EXPECT_EQ(u"Pause playback in “Inbox”",
            PauseOtherProfileMediaLabel(u"Inbox", false));
}

}  // namespace
}  // namespace ahoi::sidebar
