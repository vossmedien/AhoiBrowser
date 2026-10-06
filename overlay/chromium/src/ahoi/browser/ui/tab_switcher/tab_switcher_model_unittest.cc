// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/tab_switcher/tab_switcher_model.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::tab_switcher {
namespace {

base::TimeTicks At(int seconds) {
  return base::TimeTicks() + base::Seconds(seconds);
}

TabInput Tab(int id, bool saved, int last_active, bool active = false,
             std::optional<int> split = std::nullopt) {
  return {.id = id,
          .title = u"T" + std::u16string(1, u'0' + id),
          .url = GURL("https://site" + std::to_string(id) + ".example/a"),
          .saved = saved,
          .split = split,
          .last_active = At(last_active),
          .active = active};
}

TEST(TabSwitcherModelTest, SectionsThenRecencyAndSplitsCollapse) {
  const std::vector<Entry> entries = BuildEntries({
      Tab(1, /*saved=*/false, 10),
      Tab(2, /*saved=*/true, 30, /*active=*/true),
      Tab(3, /*saved=*/true, 20),
      Tab(4, /*saved=*/false, 5, false, /*split=*/7),
      Tab(5, /*saved=*/true, 40, false, /*split=*/7),
  });
  ASSERT_EQ(4u, entries.size());
  EXPECT_EQ(Section::kSaved, entries[0].section);
  EXPECT_EQ(std::vector<int>{2}, entries[0].tab_ids);
  EXPECT_EQ(std::vector<int>{3}, entries[1].tab_ids);
  EXPECT_EQ(Section::kTemporary, entries[2].section);
  EXPECT_EQ(Section::kSplits, entries[3].section);
  EXPECT_EQ((std::vector<int>{4, 5}), entries[3].tab_ids);
  // The split shows its most recently active pane.
  EXPECT_EQ(u"T5", entries[3].title);
  EXPECT_EQ(u"site5.example", entries[3].host);
  // The previous tab (the split, used at 40) is focused first, not the
  // active one.
  EXPECT_EQ(3u, InitialFocus(entries));
}

TEST(TabSwitcherModelTest, KeepsTheMostRecentEighteen) {
  std::vector<TabInput> tabs;
  for (int id = 1; id <= 20; ++id) {
    tabs.push_back(Tab(id, /*saved=*/false, id));
  }
  const std::vector<Entry> entries = BuildEntries(tabs);
  ASSERT_EQ(kMaxEntries, entries.size());
  EXPECT_EQ(std::vector<int>{20}, entries.front().tab_ids);
  EXPECT_EQ(std::vector<int>{3}, entries.back().tab_ids);
}

TEST(TabSwitcherModelTest, GridMovementFollowsSectionRows) {
  // Saved: 4 entries (rows 0-1), temporary: 2 entries (row 2).
  std::vector<TabInput> tabs;
  for (int id = 1; id <= 4; ++id) {
    tabs.push_back(Tab(id, /*saved=*/true, 100 - id));
  }
  tabs.push_back(Tab(5, /*saved=*/false, 50));
  tabs.push_back(Tab(6, /*saved=*/false, 40));
  const std::vector<Entry> entries = BuildEntries(tabs);
  ASSERT_EQ(6u, entries.size());
  EXPECT_EQ(3u, MoveFocus(entries, 0, Move::kDown));
  // Row 1 has one entry: column 2 falls back to it.
  EXPECT_EQ(3u, MoveFocus(entries, 2, Move::kDown));
  EXPECT_EQ(4u, MoveFocus(entries, 3, Move::kDown));
  EXPECT_EQ(3u, MoveFocus(entries, 5, Move::kUp));
  EXPECT_EQ(1u, MoveFocus(entries, 1, Move::kUp));
  EXPECT_EQ(5u, MoveFocus(entries, 5, Move::kDown));
  EXPECT_EQ(0u, MoveFocus(entries, 0, Move::kLeft));
  EXPECT_EQ(5u, MoveFocus(entries, 5, Move::kRight));
  EXPECT_EQ(0u, MoveFocus(entries, 5, Move::kNext));
}

}  // namespace
}  // namespace ahoi::tab_switcher
