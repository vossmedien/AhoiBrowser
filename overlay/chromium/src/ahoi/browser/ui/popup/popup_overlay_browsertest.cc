// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <set>

#include "ahoi/browser/ui/popup/popup_overlay_controller.h"
#include "ahoi/browser/ui/popup/popup_overlay_view.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"

namespace ahoi::popup_ui {

class AhoiPopupOverlayBrowserTest : public InProcessBrowserTest {
 protected:
  PopupOverlayController* controller() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->ahoi_popup_overlay_controller();
  }

  std::unique_ptr<content::WebContents> CreatePopupContents() {
    return content::WebContents::Create(
        content::WebContents::CreateParams(browser()->GetProfile()));
  }

  content::WebContents* ShowEligiblePopup() {
    content::WebContents* const opener =
        browser()->tab_strip_model()->GetActiveWebContents();
    std::unique_ptr<content::WebContents> popup = CreatePopupContents();
    content::WebContents* const identity = popup.get();
    blink::mojom::WindowFeatures features;
    features.has_width = true;
    features.has_height = true;
    features.bounds = gfx::Rect(0, 0, 460, 340);
    bool was_blocked = false;
    EXPECT_EQ(identity,
              opener->GetDelegate()->AddNewContents(
                  opener, std::move(popup), GURL("https://popup.example.test/"),
                  WindowOpenDisposition::NEW_POPUP, features,
                  /*user_gesture=*/true, &was_blocked));
    EXPECT_FALSE(was_blocked);
    EXPECT_TRUE(controller()->IsShowing());
    return identity;
  }
};

IN_PROC_BROWSER_TEST_F(AhoiPopupOverlayBrowserTest,
                       RejectedPopupPreservesNativeFallbackOwnership) {
  std::unique_ptr<content::WebContents> popup = CreatePopupContents();
  content::WebContents* const identity = popup.get();
  blink::mojom::WindowFeatures features;

  EXPECT_FALSE(controller()->TryShow(
      browser()->tab_strip_model()->GetActiveWebContents(), &popup,
      GURL("https://accounts.example.test/oauth/authorize"),
      WindowOpenDisposition::NEW_POPUP, features,
      /*user_gesture=*/true));
  EXPECT_EQ(identity, popup.get());
  EXPECT_FALSE(controller()->IsShowing());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(AhoiPopupOverlayBrowserTest,
                       KeyboardPromotionKeepsExactWebContentsAndState) {
  content::WebContents* const popup = ShowEligiblePopup();
  PopupOverlayView* const view = controller()->popup_view_for_testing();
  ASSERT_TRUE(view);
  view->DeprecatedLayoutImmediately();

  EXPECT_EQ(
      BrowserView::GetBrowserViewForBrowser(browser())->contents_container(),
      view->parent());
  EXPECT_EQ(gfx::Size(460, 340), view->card_for_testing()->size());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_EQ(TabStripModel::kNoTab,
            browser()->tab_strip_model()->GetIndexOfWebContents(popup));

  ASSERT_TRUE(view->GetFocusManager());
  EXPECT_TRUE(view->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_RETURN, ui::EF_COMMAND_DOWN)));

  EXPECT_FALSE(controller()->IsShowing());
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_NE(TabStripModel::kNoTab,
            browser()->tab_strip_model()->GetIndexOfWebContents(popup));
  EXPECT_EQ(popup, browser()->tab_strip_model()->GetActiveWebContents());
}

IN_PROC_BROWSER_TEST_F(AhoiPopupOverlayBrowserTest,
                       KeyboardSplitUsesUniqueTwoPhasePaneBindings) {
  content::WebContents* const opener =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* const popup = ShowEligiblePopup();
  PopupOverlayView* const view = controller()->popup_view_for_testing();
  ASSERT_TRUE(view);
  ASSERT_TRUE(view->GetFocusManager());

  EXPECT_TRUE(view->GetFocusManager()->ProcessAccelerator(ui::Accelerator(
      ui::VKEY_RETURN, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)));

  TabStripModel* const model = browser()->tab_strip_model();
  ASSERT_EQ(2, model->count());
  const int opener_index = model->GetIndexOfWebContents(opener);
  const int popup_index = model->GetIndexOfWebContents(popup);
  ASSERT_NE(TabStripModel::kNoTab, opener_index);
  ASSERT_NE(TabStripModel::kNoTab, popup_index);
  ASSERT_TRUE(model->GetTabAtIndex(opener_index)->IsSplit());
  ASSERT_TRUE(model->GetTabAtIndex(popup_index)->IsSplit());
  EXPECT_EQ(model->GetTabAtIndex(opener_index)->GetSplit(),
            model->GetTabAtIndex(popup_index)->GetSplit());

  MultiContentsView* const multi_contents =
      BrowserView::GetBrowserViewForBrowser(browser())->multi_contents_view();
  ASSERT_EQ(2u, multi_contents->GetVisiblePaneCount());
  multi_contents->GetAccessiblePanes();
  for (size_t index = 0; index < multi_contents->GetVisiblePaneCount();
       ++index) {
    ui::AXNodeData pane_data;
    multi_contents->contents_container_views()[index]
        ->contents_view()
        ->GetViewAccessibility()
        .GetAccessibleNodeData(&pane_data);
    EXPECT_EQ(static_cast<int>(index + 1u),
              pane_data.GetIntAttribute(ax::mojom::IntAttribute::kPosInSet));
    EXPECT_EQ(2, pane_data.GetIntAttribute(ax::mojom::IntAttribute::kSetSize));
    EXPECT_EQ(index == static_cast<size_t>(multi_contents->GetActiveIndex()),
              pane_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
    EXPECT_FALSE(
        pane_data.GetString16Attribute(ax::mojom::StringAttribute::kName)
            .empty());
  }
  ui::AXNodeData divider_data;
  multi_contents->resize_area_for_testing()
      ->resize_handle_for_testing()
      ->GetViewAccessibility()
      .GetAccessibleNodeData(&divider_data);
  EXPECT_EQ(ax::mojom::Role::kSlider, divider_data.role);
  EXPECT_FLOAT_EQ(0.0f, divider_data.GetFloatAttribute(
                            ax::mojom::FloatAttribute::kMinValueForRange));
  EXPECT_FLOAT_EQ(100.0f, divider_data.GetFloatAttribute(
                              ax::mojom::FloatAttribute::kMaxValueForRange));
  EXPECT_FLOAT_EQ(50.0f, divider_data.GetFloatAttribute(
                             ax::mojom::FloatAttribute::kValueForRange));
  std::set<content::WebContents*> hosted_contents;
  for (size_t index = 0; index < multi_contents->GetVisiblePaneCount();
       ++index) {
    content::WebContents* const hosted =
        multi_contents->contents_container_views()[index]
            ->contents_view()
            ->web_contents();
    ASSERT_TRUE(hosted);
    EXPECT_TRUE(hosted_contents.insert(hosted).second)
        << "A WebContents must never be attached to two split pane hosts";
  }
  EXPECT_EQ(std::set<content::WebContents*>({opener, popup}), hosted_contents);
}

}  // namespace ahoi::popup_ui
