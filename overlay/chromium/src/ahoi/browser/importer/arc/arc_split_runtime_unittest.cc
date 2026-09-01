// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_runtime.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc::internal {

TEST(ArcSplitRuntimeTest, ExactFocusRequiresActiveTargetWindowAndTab) {
  EXPECT_EQ(ArcSplitVerification::kExact,
            ClassifyArcSplitFocus(/*target_window_active=*/true,
                                  /*focused_tab_present=*/true,
                                  /*focused_tab_in_target_window=*/true,
                                  /*focused_tab_active=*/true));
  EXPECT_EQ(ArcSplitVerification::kRepairableMissing,
            ClassifyArcSplitFocus(/*target_window_active=*/false,
                                  /*focused_tab_present=*/true,
                                  /*focused_tab_in_target_window=*/true,
                                  /*focused_tab_active=*/true));
  EXPECT_EQ(ArcSplitVerification::kRepairableMissing,
            ClassifyArcSplitFocus(/*target_window_active=*/true,
                                  /*focused_tab_present=*/true,
                                  /*focused_tab_in_target_window=*/false,
                                  /*focused_tab_active=*/true));
  EXPECT_EQ(ArcSplitVerification::kRepairableMissing,
            ClassifyArcSplitFocus(/*target_window_active=*/true,
                                  /*focused_tab_present=*/true,
                                  /*focused_tab_in_target_window=*/true,
                                  /*focused_tab_active=*/false));
}

}  // namespace ahoi::importer::arc::internal
