// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/existing_base_sub_menu_model.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_menu_model.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
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

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       DragExtractionKeepsFourPaneRemainderAsThreePaneSplit) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(4, tab_strip_model->count());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0, 1, 2},
      split_tabs::SplitTabVisualData::ForFourPane(
          split_tabs::SplitTabLayout::kStacked),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  ASSERT_EQ(4u, original_panes.size());
  std::vector<content::WebContents*> original_contents;
  for (tabs::TabInterface* pane : original_panes) {
    original_contents.push_back(pane->GetContents());
  }

  tabs::TabInterface* const extracted = original_panes[1];
  ASSERT_TRUE(sidebar::ExtractTabFromSplitPreservingRemainder(tab_strip_model,
                                                              extracted));

  EXPECT_EQ(4, tab_strip_model->count());
  EXPECT_FALSE(extracted->IsSplit());
  ASSERT_TRUE(tab_strip_model->ContainsSplit(split_id));
  const std::vector<tabs::TabInterface*> remaining =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  EXPECT_EQ((std::vector<tabs::TabInterface*>{
                original_panes[0], original_panes[2], original_panes[3]}),
            remaining);
  EXPECT_EQ(
      split_tabs::SplitTabLayout::kStacked,
      tab_strip_model->GetSplitData(split_id)->visual_data()->split_layout());
  for (size_t index = 0; index < original_panes.size(); ++index) {
    EXPECT_EQ(original_contents[index], original_panes[index]->GetContents());
  }
}

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       DragExtractionKeepsThreePaneRemainderAsTwoPaneSplit) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(3, tab_strip_model->count());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0, 1},
      split_tabs::SplitTabVisualData::ForThreePane(
          split_tabs::SplitTabLayout::kStacked),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  ASSERT_EQ(3u, original_panes.size());
  std::vector<content::WebContents*> original_contents;
  for (tabs::TabInterface* pane : original_panes) {
    original_contents.push_back(pane->GetContents());
  }

  tabs::TabInterface* const extracted = original_panes[1];
  ASSERT_TRUE(sidebar::ExtractTabFromSplitPreservingRemainder(tab_strip_model,
                                                              extracted));

  EXPECT_EQ(3, tab_strip_model->count());
  EXPECT_FALSE(extracted->IsSplit());
  ASSERT_TRUE(tab_strip_model->ContainsSplit(split_id));
  EXPECT_EQ(
      (std::vector<tabs::TabInterface*>{original_panes[0], original_panes[2]}),
      tab_strip_model->GetSplitData(split_id)->ListTabs());
  for (tabs::TabInterface* pane :
       tab_strip_model->GetSplitData(split_id)->ListTabs()) {
    ASSERT_TRUE(pane->GetSplit().has_value());
    EXPECT_EQ(split_id, *pane->GetSplit());
  }
  EXPECT_EQ(
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kStacked),
      *tab_strip_model->GetSplitData(split_id)->visual_data());
  for (size_t index = 0; index < original_panes.size(); ++index) {
    EXPECT_EQ(original_contents[index], original_panes[index]->GetContents());
  }
}

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       DragExtractionDissolvesTwoPaneSplit) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(2, tab_strip_model->count());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  ASSERT_EQ(2u, original_panes.size());
  content::WebContents* const first_contents = original_panes[0]->GetContents();
  content::WebContents* const second_contents =
      original_panes[1]->GetContents();
  tabs::TabInterface* const original_active = tab_strip_model->GetActiveTab();

  ASSERT_TRUE(sidebar::ExtractTabFromSplitPreservingRemainder(
      tab_strip_model, original_panes[0]));

  EXPECT_EQ(2, tab_strip_model->count());
  EXPECT_FALSE(tab_strip_model->ContainsSplit(split_id));
  EXPECT_FALSE(original_panes[0]->IsSplit());
  EXPECT_FALSE(original_panes[1]->IsSplit());
  EXPECT_EQ(original_panes[0], tab_strip_model->GetTabAtIndex(0));
  EXPECT_EQ(original_panes[1], tab_strip_model->GetTabAtIndex(1));
  EXPECT_EQ(original_active, tab_strip_model->GetActiveTab());
  EXPECT_EQ(first_contents, original_panes[0]->GetContents());
  EXPECT_EQ(second_contents, original_panes[1]->GetContents());
}

IN_PROC_BROWSER_TEST_F(SplitLayoutMenuBrowserTest,
                       DragExtractionRejectsUnsplitSourceWithoutMutation) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(3, tab_strip_model->count());
  tabs::TabInterface* const unsplit_tab = tab_strip_model->GetTabAtIndex(1);
  ASSERT_TRUE(unsplit_tab);
  ASSERT_FALSE(unsplit_tab->IsSplit());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_FALSE(unsplit_tab->IsSplit());

  std::vector<tabs::TabInterface*> original_tabs;
  std::vector<content::WebContents*> original_contents;
  for (int index = 0; index < tab_strip_model->count(); ++index) {
    tabs::TabInterface* const tab = tab_strip_model->GetTabAtIndex(index);
    original_tabs.push_back(tab);
    original_contents.push_back(tab->GetContents());
  }
  tabs::TabInterface* const original_active = tab_strip_model->GetActiveTab();
  const std::vector<tabs::TabInterface*> original_panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  const split_tabs::SplitTabVisualData original_visual_data =
      *tab_strip_model->GetSplitData(split_id)->visual_data();

  EXPECT_FALSE(
      sidebar::ExtractTabFromSplitPreservingRemainder(nullptr, unsplit_tab));
  EXPECT_FALSE(sidebar::ExtractTabFromSplitPreservingRemainder(tab_strip_model,
                                                               nullptr));
  EXPECT_FALSE(sidebar::ExtractTabFromSplitPreservingRemainder(tab_strip_model,
                                                               unsplit_tab));

  ASSERT_EQ(static_cast<int>(original_tabs.size()), tab_strip_model->count());
  for (size_t index = 0; index < original_tabs.size(); ++index) {
    EXPECT_EQ(original_tabs[index],
              tab_strip_model->GetTabAtIndex(static_cast<int>(index)));
    EXPECT_EQ(original_contents[index], original_tabs[index]->GetContents());
  }
  EXPECT_EQ(original_active, tab_strip_model->GetActiveTab());
  ASSERT_TRUE(tab_strip_model->ContainsSplit(split_id));
  EXPECT_EQ(original_panes,
            tab_strip_model->GetSplitData(split_id)->ListTabs());
  EXPECT_EQ(original_visual_data,
            *tab_strip_model->GetSplitData(split_id)->visual_data());
  EXPECT_FALSE(unsplit_tab->IsSplit());
}

}  // namespace
}  // namespace ahoi::split_drop
