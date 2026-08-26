// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/existing_base_sub_menu_model.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/split_tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::split_drop {
namespace {

int CommandId(SplitTabMenuModel::CommandId command) {
  return ExistingBaseSubMenuModel::kMinSplitTabMenuModelCommandId +
         static_cast<int>(command);
}

class SplitLayoutMenuBrowserTest : public InProcessBrowserTest {
 public:
  SplitLayoutMenuBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(tabs::kSplitViewHorizontal);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       ThreePanePresetsKeepWebContents) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(tab_strip_model->count(), 3);
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0, 1},
      split_tabs::SplitTabVisualData::ForThreePane(
          split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_tabs =
      tab_strip_model->GetSplitData(split_id)->ListTabs();

  SplitTabMenuModel menu(tab_strip_model,
                         SplitTabMenuModel::MenuSource::kTabContextMenu, 2);
  const size_t columns_index =
      menu.GetIndexOfCommandId(
              CommandId(SplitTabMenuModel::CommandId::kThreeColumns))
          .value();
  const size_t main_bottom_index =
      menu.GetIndexOfCommandId(
              CommandId(SplitTabMenuModel::CommandId::kThreeMainBottom))
          .value();
  EXPECT_TRUE(menu.IsItemCheckedAt(columns_index));
  EXPECT_FALSE(menu.IsItemCheckedAt(main_bottom_index));

  menu.ActivatedAt(main_bottom_index);
  const split_tabs::SplitTabVisualData* const visual_data =
      tab_strip_model->GetSplitData(split_id)->visual_data();
  EXPECT_EQ(visual_data->split_layout(), split_tabs::SplitTabLayout::kStacked);
  EXPECT_EQ(visual_data->arrangement(),
            split_tabs::SplitTabArrangement::kMainEnd);
  EXPECT_DOUBLE_EQ(visual_data->split_ratio(), 0.5);
  EXPECT_DOUBLE_EQ(visual_data->secondary_split_ratio(), 0.5);
  EXPECT_TRUE(menu.IsItemCheckedAt(main_bottom_index));
  EXPECT_EQ(tab_strip_model->GetSplitData(split_id)->ListTabs(), original_tabs);

  menu.ActivatedAt(columns_index);
  EXPECT_EQ(visual_data->split_layout(),
            split_tabs::SplitTabLayout::kSideBySide);
  EXPECT_EQ(visual_data->arrangement(),
            split_tabs::SplitTabArrangement::kLinear);
  EXPECT_DOUBLE_EQ(visual_data->split_ratio(), 1.0 / 3.0);
  EXPECT_DOUBLE_EQ(visual_data->secondary_split_ratio(), 0.5);
  EXPECT_TRUE(menu.IsItemCheckedAt(columns_index));
  EXPECT_EQ(tab_strip_model->GetSplitData(split_id)->ListTabs(), original_tabs);
}

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       FourPanePresetsKeepWebContents) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(tab_strip_model->count(), 4);
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0, 1, 2},
      split_tabs::SplitTabVisualData::ForFourPane(
          split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_tabs =
      tab_strip_model->GetSplitData(split_id)->ListTabs();

  SplitTabMenuModel menu(tab_strip_model,
                         SplitTabMenuModel::MenuSource::kTabContextMenu, 3);
  const size_t columns_index =
      menu.GetIndexOfCommandId(
              CommandId(SplitTabMenuModel::CommandId::kFourGridColumnsFirst))
          .value();
  const size_t rows_index =
      menu.GetIndexOfCommandId(
              CommandId(SplitTabMenuModel::CommandId::kFourGridRowsFirst))
          .value();
  EXPECT_TRUE(menu.IsItemCheckedAt(columns_index));
  EXPECT_FALSE(menu.IsItemCheckedAt(rows_index));

  menu.ActivatedAt(rows_index);
  const split_tabs::SplitTabVisualData* const visual_data =
      tab_strip_model->GetSplitData(split_id)->visual_data();
  EXPECT_EQ(visual_data->split_layout(), split_tabs::SplitTabLayout::kStacked);
  EXPECT_EQ(visual_data->arrangement(),
            split_tabs::SplitTabArrangement::kLinear);
  EXPECT_DOUBLE_EQ(visual_data->split_ratio(), 0.5);
  EXPECT_DOUBLE_EQ(visual_data->secondary_split_ratio(), 0.5);
  EXPECT_TRUE(menu.IsItemCheckedAt(rows_index));
  EXPECT_EQ(tab_strip_model->GetSplitData(split_id)->ListTabs(), original_tabs);
}

}  // namespace
}  // namespace ahoi::split_drop
