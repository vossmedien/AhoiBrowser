// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include "ahoi/browser/session/workspace_structure_controller.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {
namespace {

sync::SharedSplitMetadata Split(const base::Uuid& id, const base::Uuid& workspace) {
  sync::SharedSplitMetadata split;
  split.id = id;
  split.workspace_id = workspace;
  return split;
}

class SplitCaptureTest : public testing::Test {
 protected:
  const base::Uuid id_ = base::Uuid::GenerateRandomV4();
  const base::Uuid a_ = base::Uuid::GenerateRandomV4();
  const base::Uuid b_ = base::Uuid::GenerateRandomV4();
};

// Handoff 011 S6: moving a split's pages to another Workspace changes no
// native split, but the record follows its members.
TEST_F(SplitCaptureTest, WorkspaceOnlyMoveIsAdoptedWithoutNativeChange) {
  EXPECT_EQ(SplitCaptureAdoption::kWorkspaceOnly,
            ClassifySplitCapture(/*native_change=*/false,
                                 /*record_tombstone=*/false, Split(id_, a_),
                                 Split(id_, b_), a_));
}

TEST_F(SplitCaptureTest, NativeChangeIsAdopted) {
  EXPECT_EQ(SplitCaptureAdoption::kNativeChange,
            ClassifySplitCapture(true, false, Split(id_, a_), Split(id_, b_),
                                 a_));
}

TEST_F(SplitCaptureTest, UnchangedTombstonedOrUnobservedIsIgnored) {
  EXPECT_EQ(SplitCaptureAdoption::kNone,
            ClassifySplitCapture(false, false, Split(id_, a_), Split(id_, a_),
                                 a_));
  EXPECT_EQ(SplitCaptureAdoption::kNone,
            ClassifySplitCapture(true, true, Split(id_, a_), Split(id_, b_),
                                 a_));
  EXPECT_EQ(SplitCaptureAdoption::kNone,
            ClassifySplitCapture(true, false, std::nullopt, Split(id_, b_), a_));
}

TEST_F(SplitCaptureTest, UnobservedLayoutChangeIsNotAdoptedAsWorkspaceMove) {
  sync::SharedSplitMetadata current = Split(id_, b_);
  current.ratios.primary = 300000;
  current.ratios.secondary = 700000;
  EXPECT_EQ(SplitCaptureAdoption::kNone,
            ClassifySplitCapture(false, false, Split(id_, a_), current, a_));
}

TEST_F(SplitCaptureTest, RecordAlreadyInTheMembersWorkspaceNeedsNoMove) {
  EXPECT_EQ(SplitCaptureAdoption::kNone,
            ClassifySplitCapture(false, false, Split(id_, a_), Split(id_, b_),
                                 b_));
}

// Handoff 058: a split token from an archive whose page did not close (hung
// renderer) expires, so a later manual dissolve tombstones the record again.
TEST(ArchiveCloseTokenTest, ExpiresAfterTheGrace) {
  const base::TimeTicks marked = base::TimeTicks() + base::Seconds(100);
  EXPECT_TRUE(ArchiveCloseTokenLive(marked, marked));
  EXPECT_TRUE(
      ArchiveCloseTokenLive(marked, marked + kArchiveCloseGrace -
                                        base::Milliseconds(1)));
  EXPECT_FALSE(ArchiveCloseTokenLive(marked, marked + kArchiveCloseGrace));
  EXPECT_FALSE(ArchiveCloseTokenLive(marked, marked + base::Minutes(5)));
}

}  // namespace
}  // namespace ahoi::session
