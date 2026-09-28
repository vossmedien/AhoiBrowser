// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/uuid.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace ahoi::sidebar {
namespace {

class SidebarTabDensityTest : public views::ViewsTestBase {};

TEST_F(SidebarTabDensityTest, SavedRemoteAndSplitRowsShareSemanticHeight) {
  EXPECT_EQ(40, visual_style::kSidebarTabRowHeight);
  EXPECT_EQ(visual_style::kSidebarTabRowHeight, SidebarTreeRowView::kRowHeight);

  RemoteTabRowModel remote_model;
  remote_model.tab.id = base::Uuid::GenerateRandomV4();
  remote_model.tab.device_id = base::Uuid::GenerateRandomV4();
  remote_model.tab.session_id = base::Uuid::GenerateRandomV4();
  remote_model.tab.url = "https://example.test/remote";
  remote_model.tab.title = "Remote tab";
  remote_model.device_name = u"Remote Mac";
  remote_model.relative_activity = u"Now";
  remote_model.remote_status = u"Online";
  std::unique_ptr<views::View> remote_row =
      CreateRemoteTabRowView(std::move(remote_model), {});
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            remote_row->GetPreferredSize().height());

  // A side-by-side split row keeps the standard height. The composite view
  // itself requires real runtime tab rows (CHECKed), so check its layout.
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            GetSplitRowPreferredHeight(
                2,
                split_tabs::SplitTabVisualData(
                    split_tabs::SplitTabLayout::kSideBySide),
                visual_style::kSidebarTabRowHeight));
}

}  // namespace
}  // namespace ahoi::sidebar
