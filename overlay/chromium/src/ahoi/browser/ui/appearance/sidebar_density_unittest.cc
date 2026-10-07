// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_density.h"

#include <memory>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/appearance/sidebar_density_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"
#include "ahoi/browser/ui/sidebar/sidebar_remote_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_test_support.h"
#include "base/functional/bind.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "ui/views/controls/label.h"

namespace ahoi::sidebar {
namespace {

TEST(SidebarDensityPrefsTest, DefaultsInvalidValuesAndScopedReset) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  appearance::RegisterProfilePrefs(prefs.registry());
  EXPECT_EQ(appearance::SidebarDensity::kStandard,
            appearance::GetSidebarDensity(prefs));
  prefs.SetBoolean(appearance::kSidebarPageTintEnabledPref, false);
  int notifications = 0;
  PrefChangeRegistrar observer;
  observer.Init(&prefs);
  observer.Add(appearance::kSidebarDensityPref,
               base::BindRepeating([](int* count) { ++*count; },
                                   &notifications));
  prefs.SetInteger(appearance::kSidebarDensityPref, 0);
  EXPECT_EQ(appearance::SidebarDensity::kCompact,
            appearance::GetSidebarDensity(prefs));
  prefs.SetInteger(appearance::kSidebarDensityPref, 2);
  EXPECT_EQ(appearance::SidebarDensity::kComfortable,
            appearance::GetSidebarDensity(prefs));
  prefs.SetInteger(appearance::kSidebarDensityPref, 99);
  EXPECT_EQ(appearance::SidebarDensity::kStandard,
            appearance::GetSidebarDensity(prefs));
  EXPECT_TRUE(appearance::ResetSidebarDensity(prefs));
  EXPECT_FALSE(prefs.GetUserPrefValue(appearance::kSidebarDensityPref));
  EXPECT_FALSE(appearance::IsSidebarPageTintEnabled(prefs));
  EXPECT_EQ(4, notifications);
  prefs.SetManagedPref(appearance::kSidebarDensityPref, base::Value(2));
  EXPECT_FALSE(appearance::ResetSidebarDensity(prefs));
  EXPECT_EQ(appearance::SidebarDensity::kComfortable,
            appearance::GetSidebarDensity(prefs));
}

TEST_F(SidebarTreeViewTest, DensityChangesSavedAndRemoteRowsWithoutFontDrift) {
  const auto workspace = MakeWorkspace();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            store_.CreateWorkspace(workspace));
  const auto page = MakeNode(workspace, std::nullopt,
                            tab_tree::TreeNodeType::kSavedPage, u"Page", "a");
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk, store_.CreateNode(page));
  auto tree = NewTreeView();
  ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
            controller_->ActivateWorkspace(workspace.id));
  views::View root;
  auto* tree_view = root.AddChildView(std::move(tree));
  auto* label = root.AddChildView(std::make_unique<views::Label>(u"Title"));
  const int original_font_size = label->font_list().GetFontSize();
  RemoteTabRowModel remote;
  remote.tab.title = "Remote";
  auto* remote_row = root.AddChildView(CreateRemoteTabRowView(remote, {}));

  for (const auto density : {appearance::SidebarDensity::kCompact,
                             appearance::SidebarDensity::kComfortable,
                             appearance::SidebarDensity::kStandard}) {
    appearance::SetSidebarDensityForView(&root, density);
    tree_view->OnSidebarDensityChanged();
    appearance::ApplySidebarDensityFont(label);
    appearance::ApplySidebarDensityFont(label);
    const auto metrics = appearance::GetSidebarDensityMetrics(density);
    EXPECT_GE(metrics.row_height, 32);
    EXPECT_GE(metrics.split_pane_minimum_height, 30);
    EXPECT_GE(GetSidebarTabTrailingLayout(240, metrics.row_height, false)
                  .hover_action.height(),
              32);
    EXPECT_EQ(metrics.row_height,
              GetSplitRowPreferredHeight(
                  2, split_tabs::SplitTabVisualData(
                         split_tabs::SplitTabLayout::kSideBySide),
                  metrics.row_height, metrics.split_pane_minimum_height));
    EXPECT_GE(GetSplitRowPreferredHeight(
                  2, split_tabs::SplitTabVisualData(
                         split_tabs::SplitTabLayout::kStacked),
                  metrics.row_height, metrics.split_pane_minimum_height),
              2 * metrics.split_pane_minimum_height);
    EXPECT_EQ(metrics.row_height, remote_row->GetPreferredSize().height());
    EXPECT_EQ(metrics.row_height + SidebarTreeView::kRootAppendDropHeight,
              tree_view->GetPreferredSize().height());
    tree_view->SynchronizeRowsForTesting(gfx::Rect(0, 0, 240, 200));
    auto* saved_row = tree_view->GetMaterializedRowForTesting(page.id);
    ASSERT_TRUE(saved_row);
    EXPECT_EQ(metrics.row_height, saved_row->height());
    EXPECT_EQ(original_font_size + metrics.font_size_delta,
              label->font_list().GetFontSize());
    EXPECT_EQ(views::View::FocusBehavior::ALWAYS,
              remote_row->GetFocusBehavior());
    ui::AXNodeData remote_data;
    remote_row->GetViewAccessibility().GetAccessibleNodeData(&remote_data);
    EXPECT_EQ(ax::mojom::Role::kTab, remote_data.role);
    ui::AXNodeData saved_data;
    saved_row->GetViewAccessibility().GetAccessibleNodeData(&saved_data);
    EXPECT_EQ(ax::mojom::Role::kTreeItem, saved_data.role);
  }
  views::View other_profile_root;
  EXPECT_EQ(visual_style::kSidebarTabRowHeight,
            appearance::GetSidebarDensityMetricsForView(&other_profile_root)
                .row_height);
}

}  // namespace
}  // namespace ahoi::sidebar
