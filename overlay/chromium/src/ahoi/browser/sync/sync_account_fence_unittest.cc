// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_account_fence.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Action = SyncAccountFence::Action;
using Verdict = SyncAccountFence::Verdict;

TEST(SyncAccountFenceTest, StartsOpen) {
  SyncAccountFence fence;
  EXPECT_TRUE(fence.open());
}

// 30 Sep 2026, 01:47:19: a notification for the same account must not end
// in "iCloud-Accountwechsel benötigt Bestätigung".
TEST(SyncAccountFenceTest, SameAccountReopensAfterNotification) {
  SyncAccountFence fence;
  const uint64_t check = fence.Close();
  EXPECT_FALSE(fence.open());
  EXPECT_EQ(Action::kReopen, fence.OnVerified(check, Verdict::kSameAccount));
  EXPECT_TRUE(fence.open());
}

TEST(SyncAccountFenceTest, RealSwitchOrSignOutRevokes) {
  SyncAccountFence other;
  const uint64_t switched = other.Close();
  EXPECT_EQ(Action::kRevoke,
            other.OnVerified(switched, Verdict::kOtherAccount));
  EXPECT_FALSE(other.open());

  SyncAccountFence signed_out;
  const uint64_t gone = signed_out.Close();
  EXPECT_EQ(Action::kRevoke,
            signed_out.OnVerified(gone, Verdict::kUnavailable));
  EXPECT_FALSE(signed_out.open());
}

TEST(SyncAccountFenceTest, FailedCheckStaysFencedAndRetries) {
  SyncAccountFence fence;
  const uint64_t check = fence.Close();
  EXPECT_EQ(Action::kRetry, fence.OnVerified(check, Verdict::kError));
  EXPECT_FALSE(fence.open());
  // The retry of the same check may still reopen.
  EXPECT_EQ(Action::kReopen, fence.OnVerified(check, Verdict::kSameAccount));
  EXPECT_TRUE(fence.open());
}

TEST(SyncAccountFenceTest, OnlyTheNewestCheckDecides) {
  SyncAccountFence fence;
  const uint64_t first = fence.Close();
  const uint64_t second = fence.Close();
  // A check that started before the second notification cannot reopen.
  EXPECT_EQ(Action::kIgnore, fence.OnVerified(first, Verdict::kSameAccount));
  EXPECT_FALSE(fence.open());
  EXPECT_EQ(Action::kIgnore, fence.OnVerified(first, Verdict::kOtherAccount));
  EXPECT_EQ(Action::kReopen, fence.OnVerified(second, Verdict::kSameAccount));
  EXPECT_TRUE(fence.open());
}

TEST(SyncAccountFenceTest, StaleOrUnknownChecksAreIgnoredWhenOpen) {
  SyncAccountFence fence;
  EXPECT_EQ(Action::kIgnore, fence.OnVerified(0, Verdict::kOtherAccount));
  const uint64_t check = fence.Close();
  ASSERT_EQ(Action::kReopen, fence.OnVerified(check, Verdict::kSameAccount));
  // A duplicate completion after reopening changes nothing.
  EXPECT_EQ(Action::kIgnore, fence.OnVerified(check, Verdict::kOtherAccount));
  EXPECT_TRUE(fence.open());
}

TEST(SyncAccountFenceTest, NotificationAfterReopenFencesAgain) {
  SyncAccountFence fence;
  ASSERT_EQ(Action::kReopen,
            fence.OnVerified(fence.Close(), Verdict::kSameAccount));
  const uint64_t next = fence.Close();
  EXPECT_FALSE(fence.open());
  EXPECT_EQ(Action::kRevoke, fence.OnVerified(next, Verdict::kOtherAccount));
  EXPECT_FALSE(fence.open());
}

}  // namespace
}  // namespace ahoi::sync
