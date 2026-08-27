// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <vector>

#include "ahoi/browser/ui/split_drop/split_drop_overlay_view.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/browser/ui/views/interaction/browser_elements_views.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace ahoi::split_drop {
namespace {

SplitDropOverlayView* FindSplitDropOverlay(MultiContentsView* view) {
  for (views::View* child : view->children()) {
    if (auto* overlay = views::AsViewClass<SplitDropOverlayView>(child)) {
      return overlay;
    }
  }
  return nullptr;
}

class SplitPaneInteractiveUiTest : public InteractiveBrowserTest {
 public:
  SplitPaneInteractiveUiTest() {
    scoped_feature_list_.InitAndEnableFeature(tabs::kSplitViewHorizontal);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(SplitPaneInteractiveUiTest,
                       NativePaneHandleDropReordersIntoBottomZoneAtomically) {
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(2, tab_strip_model->count());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> original_panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  ASSERT_EQ(2u, original_panes.size());
  content::WebContents* const first_contents = original_panes[0]->GetContents();
  content::WebContents* const second_contents =
      original_panes[1]->GetContents();
  ASSERT_TRUE(first_contents);
  ASSERT_TRUE(second_contents);
  ASSERT_NE(first_contents, second_contents);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  ASSERT_TRUE(multi_contents_view->IsInSplitView());
  ASSERT_EQ(2u, multi_contents_view->GetVisiblePaneCount());

  MultiContentsViewMiniToolbar* const source_toolbar =
      multi_contents_view->mini_toolbar_for_testing(0);
  ASSERT_TRUE(source_toolbar);
  views::View* const source_handle = source_toolbar->drag_handle_for_testing();
  ASSERT_TRUE(source_handle);
  ASSERT_TRUE(source_handle->GetVisible());
  ASSERT_FALSE(source_handle->GetBoundsInScreen().IsEmpty());

  const auto& containers = multi_contents_view->contents_container_views();
  ASSERT_GE(containers.size(), 2u);
  ASSERT_EQ(first_contents, containers[0]->contents_view()->web_contents());
  ASSERT_EQ(second_contents, containers[1]->contents_view()->web_contents());
  ASSERT_TRUE(containers[1]->GetVisible());
  const gfx::Rect target_bounds = containers[1]->GetBoundsInScreen();
  ASSERT_FALSE(target_bounds.IsEmpty());

  SplitDropOverlayView* const overlay =
      FindSplitDropOverlay(multi_contents_view);
  ASSERT_TRUE(overlay);
  ASSERT_FALSE(overlay->GetVisible());

  const gfx::Point source_point =
      source_handle->GetBoundsInScreen().CenterPoint();
  const gfx::Point threshold_point = source_point + gfx::Vector2d(12, 0);
  const gfx::Point target_point =
      target_bounds.bottom_center() - gfx::Vector2d(0, 8);
  const std::vector<tabs::TabInterface*> expected_panes = {original_panes[1],
                                                           original_panes[0]};

  ASSERT_TRUE(RunTestSequence(
      ActivateSurface(kBrowserViewElementId), MoveMouseTo(source_point),
      ClickMouse(ui_controls::LEFT, /*release=*/false),
      MoveMouseTo(threshold_point), MoveMouseTo(target_point),
      PollUntil(
          [&]() {
            const std::optional<DropIntent>& intent =
                overlay->intent_for_testing();
            return overlay->GetVisible() && intent.has_value() &&
                   intent->zone == DropZone::kBottom &&
                   intent->action == DropAction::kReorderInSplit &&
                   intent->target_pane_index == 1u &&
                   intent->layout == split_tabs::SplitTabLayout::kStacked;
          },
          "Wait for the bottom split-drop overlay"),
      ReleaseMouse(),
      PollUntil(
          [&]() {
            const split_tabs::SplitTabData* const split_data =
                tab_strip_model->GetSplitData(split_id);
            return split_data && split_data->visual_data() &&
                   split_data->visual_data()->split_layout() ==
                       split_tabs::SplitTabLayout::kStacked &&
                   split_data->visual_data()->arrangement() ==
                       split_tabs::SplitTabArrangement::kLinear &&
                   split_data->ListTabs() == expected_panes &&
                   tab_strip_model->GetActiveTab() == original_panes[0] &&
                   containers[0]->contents_view()->web_contents() ==
                       second_contents &&
                   containers[1]->contents_view()->web_contents() ==
                       first_contents &&
                   !overlay->GetVisible() &&
                   !overlay->intent_for_testing().has_value();
          },
          "Wait for the native split-drop transaction to complete"),
      Do([&]() {
        const split_tabs::SplitTabData* const split_data =
            tab_strip_model->GetSplitData(split_id);
        ASSERT_TRUE(split_data);
        ASSERT_TRUE(split_data->visual_data());
        EXPECT_EQ(2, tab_strip_model->count());
        EXPECT_EQ(split_tabs::SplitTabLayout::kStacked,
                  split_data->visual_data()->split_layout());
        EXPECT_EQ(split_tabs::SplitTabArrangement::kLinear,
                  split_data->visual_data()->arrangement());
        EXPECT_EQ(expected_panes, split_data->ListTabs());
        for (tabs::TabInterface* pane : original_panes) {
          ASSERT_TRUE(pane->GetSplit().has_value());
          EXPECT_EQ(split_id, *pane->GetSplit());
        }
        EXPECT_EQ(original_panes[0], tab_strip_model->GetActiveTab());
        EXPECT_EQ(first_contents, original_panes[0]->GetContents());
        EXPECT_EQ(second_contents, original_panes[1]->GetContents());
        EXPECT_EQ(first_contents, tab_strip_model->GetActiveWebContents());
        EXPECT_EQ(second_contents,
                  containers[0]->contents_view()->web_contents());
        EXPECT_EQ(first_contents,
                  containers[1]->contents_view()->web_contents());
        EXPECT_FALSE(overlay->GetVisible());
        EXPECT_FALSE(overlay->intent_for_testing().has_value());
      })));
}

IN_PROC_BROWSER_TEST_F(SplitPaneInteractiveUiTest,
                       PaneSelectionTargetsOmniboxNavigation) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kSelectedPaneContents);
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL first_url = embedded_test_server()->GetURL("/title1.html");
  const GURL second_url = embedded_test_server()->GetURL("/title2.html");
  const GURL replacement_url =
      embedded_test_server()->GetURL("/simple.html?active-pane");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));

  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  ASSERT_EQ(2, tab_strip_model->count());
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const std::vector<tabs::TabInterface*> panes =
      tab_strip_model->GetSplitData(split_id)->ListTabs();
  ASSERT_EQ(2u, panes.size());
  content::WebContents* const first_contents = panes[0]->GetContents();
  content::WebContents* const second_contents = panes[1]->GetContents();
  ASSERT_TRUE(first_contents);
  ASSERT_TRUE(second_contents);
  ASSERT_NE(first_contents, second_contents);
  ASSERT_EQ(first_url, first_contents->GetLastCommittedURL());
  ASSERT_EQ(second_url, second_contents->GetLastCommittedURL());

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);

  MultiContentsViewMiniToolbar* const first_toolbar =
      multi_contents_view->mini_toolbar_for_testing(0);
  MultiContentsViewMiniToolbar* const second_toolbar =
      multi_contents_view->mini_toolbar_for_testing(1);
  ASSERT_TRUE(first_toolbar);
  ASSERT_TRUE(second_toolbar);
  views::View* const first_handle = first_toolbar->drag_handle_for_testing();
  ASSERT_TRUE(first_handle);
  ASSERT_TRUE(first_toolbar->active_indicator_for_testing());
  ASSERT_TRUE(second_toolbar->active_indicator_for_testing());
  const auto& containers = multi_contents_view->contents_container_views();
  ASSERT_GE(containers.size(), 2u);
  ASSERT_EQ(first_contents, containers[0]->contents_view()->web_contents());
  ASSERT_EQ(second_contents, containers[1]->contents_view()->web_contents());
  ASSERT_EQ(panes[1], tab_strip_model->GetActiveTab());
  ASSERT_EQ(second_contents, tab_strip_model->GetActiveWebContents());
  ASSERT_FALSE(first_toolbar->active_indicator_for_testing()->GetVisible());
  ASSERT_TRUE(second_toolbar->active_indicator_for_testing()->GetVisible());

  ASSERT_TRUE(RunTestSequence(
      ActivateSurface(kBrowserViewElementId),
      MoveMouseTo(first_handle->GetBoundsInScreen().CenterPoint()),
      ClickMouse(),
      PollUntil(
          [&]() {
            return tab_strip_model->GetActiveTab() == panes[0] &&
                   tab_strip_model->GetActiveWebContents() == first_contents &&
                   first_toolbar->active_indicator_for_testing()
                       ->GetVisible() &&
                   !second_toolbar->active_indicator_for_testing()
                        ->GetVisible();
          },
          "Wait for pane selection to bind the omnibox target"),
      Do([&]() {
        EXPECT_EQ(panes[0], tab_strip_model->GetActiveTab());
        EXPECT_EQ(first_contents, tab_strip_model->GetActiveWebContents());
        EXPECT_TRUE(
            first_toolbar->active_indicator_for_testing()->GetVisible());
        EXPECT_FALSE(
            second_toolbar->active_indicator_for_testing()->GetVisible());
        if (browser_view->IsAhoiBrowserSurface()) {
          first_toolbar->UpdateState(/*is_active=*/true,
                                     /*is_highlighted=*/true);
          EXPECT_TRUE(first_toolbar->GetVisible());
          first_toolbar->UpdateState(/*is_active=*/true,
                                     /*is_highlighted=*/false);
        }
      }),
      InstrumentTab(kSelectedPaneContents), FocusElement(kOmniboxElementId),
      EnterText(kOmniboxElementId, base::UTF8ToUTF16(replacement_url.spec())),
      Confirm(kOmniboxElementId),
      WaitForWebContentsNavigation(kSelectedPaneContents, replacement_url),
      Do([&]() {
        EXPECT_EQ(first_contents, panes[0]->GetContents());
        EXPECT_EQ(second_contents, panes[1]->GetContents());
        EXPECT_EQ(replacement_url, first_contents->GetLastCommittedURL());
        EXPECT_EQ(second_url, second_contents->GetLastCommittedURL());
        EXPECT_EQ(panes[0], tab_strip_model->GetActiveTab());
        EXPECT_EQ(first_contents, tab_strip_model->GetActiveWebContents());
        EXPECT_EQ(first_contents,
                  containers[0]->contents_view()->web_contents());
        EXPECT_EQ(second_contents,
                  containers[1]->contents_view()->web_contents());
      })));
}

}  // namespace
}  // namespace ahoi::split_drop
