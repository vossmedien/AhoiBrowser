// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/tab_mru.h"

#include <set>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(TabMruTest, ReturnsPreviousTabAndTogglesBack) {
  TabMru mru;
  mru.RecordActivation(1);
  mru.RecordActivation(2);
  mru.RecordActivation(3);
  const auto any = [](int32_t) { return true; };
  EXPECT_EQ(2, mru.LastUsedBefore(3, any));
  mru.RecordActivation(2);
  EXPECT_EQ(3, mru.LastUsedBefore(2, any));
}

TEST(TabMruTest, SkipsIneligibleAndRemovedTabs) {
  TabMru mru;
  for (int32_t id : {1, 2, 3, 4}) {
    mru.RecordActivation(id);
  }
  // 3 belongs to another Workspace; 2 was closed.
  mru.Remove(2);
  const std::set<int32_t> same_workspace = {1, 2, 4};
  EXPECT_EQ(1, mru.LastUsedBefore(4, [&](int32_t id) {
    return same_workspace.contains(id);
  }));
  EXPECT_FALSE(mru.LastUsedBefore(1, [](int32_t id) { return id == 1; }));
  EXPECT_FALSE(TabMru().LastUsedBefore(1, [](int32_t) { return true; }));
}

TEST(TabMruTest, RepeatedActivationKeepsOneEntryAndIsBounded) {
  TabMru mru;
  mru.RecordActivation(7);
  mru.RecordActivation(7);
  EXPECT_EQ(1u, mru.size());
  for (int32_t id = 0; id < static_cast<int32_t>(TabMru::kMaxEntries) + 10;
       ++id) {
    mru.RecordActivation(id);
  }
  EXPECT_EQ(TabMru::kMaxEntries, mru.size());
}

}  // namespace
}  // namespace ahoi
