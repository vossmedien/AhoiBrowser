// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/drop_helper.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr char kAhoiToolkitEnabledPref[] = "ahoi.developer_toolkit.enabled";
constexpr char kAhoiShowCookieButtonPref[] =
    "ahoi.developer_toolbar.show_cookie_button";
constexpr char kAhoiShowCacheButtonPref[] =
    "ahoi.developer_toolbar.show_cache_button";
constexpr char kAhoiShowToolkitButtonPref[] =
    "ahoi.developer_toolbar.show_toolkit_button";

views::View* FindDraggableDescendant(views::View* root) {
  if (!root || !root->GetVisible()) {
    return nullptr;
  }
  if (ahoi::sidebar::GetOpenTabForView(root) && root->drag_controller()) {
    views::DragController* const controller = root->drag_controller();
    const gfx::Point press = root->GetLocalBounds().CenterPoint();
    if (!root->bounds().IsEmpty() &&
        controller->CanStartDragForView(root, press, press)) {
      return root;
    }
  }
  for (views::View* const child : root->children()) {
    if (views::View* const result = FindDraggableDescendant(child)) {
      return result;
    }
  }
  return nullptr;
}

views::View* FindAcceptingDropDescendant(views::View* root,
                                         const ui::OSExchangeData& data) {
  if (!root || !root->GetVisible()) {
    return nullptr;
  }
  for (views::View* const child : root->children()) {
    if (views::View* const result = FindAcceptingDropDescendant(child, data)) {
      return result;
    }
  }
  int formats = 0;
  std::set<ui::ClipboardFormatType> format_types;
  return root->GetDropFormats(&formats, &format_types) &&
                 data.HasAnyFormat(formats, format_types) && root->CanDrop(data)
             ? root
             : nullptr;
}

gfx::Rect BoundsInTarget(views::View* view, views::View* target) {
  return views::View::ConvertRectToTarget(view, target, view->GetLocalBounds());
}

bool IsInsideOrEqual(views::View* ancestor, views::View* candidate) {
  return candidate && (candidate == ancestor || ancestor->Contains(candidate));
}

}  // namespace

class VerticalTabStripRegionViewTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 public:
  VerticalTabStripRegionView* region_view() {
    return browser()
        ->GetBrowserView()
        .vertical_tab_strip_region_view_for_testing();
  }

  tabs::VerticalTabStripStateController* state_controller() {
    return tabs::VerticalTabStripStateController::From(browser());
  }
};

IN_PROC_BROWSER_TEST_F(VerticalTabStripRegionViewTest,
                       AhoiResizeAreaHandlesAccessibleIncrement) {
  std::unique_ptr<views::View> previous_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::make_unique<views::View>());
  ASSERT_TRUE(previous_sidebar);
  EXPECT_FALSE(region_view()->GetTopContainer()->GetVisible());
  EXPECT_FALSE(region_view()->GetBottomContainer()->GetVisible());
  const int initial_width = region_view()->GetPreferredSize().width();
  ASSERT_GE(initial_width, 220);
  ASSERT_LE(initial_width, 420);

  ui::AXActionData increment;
  increment.action = ax::mojom::Action::kIncrement;
  EXPECT_TRUE(region_view()->resize_area_for_testing()->HandleAccessibleAction(
      increment));
  const int expected_width = std::min(initial_width + 50, 420);
  EXPECT_EQ(expected_width, region_view()->GetPreferredSize().width());
  EXPECT_EQ(expected_width, state_controller()->GetUncollapsedWidth());

  std::unique_ptr<views::View> dummy_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::move(previous_sidebar));
  EXPECT_TRUE(dummy_sidebar);
  EXPECT_FALSE(region_view()->GetTopContainer()->GetVisible());
  EXPECT_FALSE(region_view()->GetBottomContainer()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(VerticalTabStripRegionViewTest,
                       AhoiKeepsStableWindowDragBand) {
  std::unique_ptr<views::View> previous_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::make_unique<views::View>());
  region_view()->SetBounds(0, 0, 300, 600);
  region_view()->DeprecatedLayoutImmediately();

  EXPECT_TRUE(region_view()->IsPositionInWindowCaption(
      gfx::Point(20, ahoi::visual_style::kSidebarTitlebarHeight - 1)));
  EXPECT_FALSE(region_view()->IsPositionInWindowCaption(
      gfx::Point(region_view()->width() - 1,
                 ahoi::visual_style::kSidebarTitlebarHeight - 1)));
  EXPECT_FALSE(region_view()->IsPositionInWindowCaption(
      gfx::Point(20, ahoi::visual_style::kSidebarTitlebarHeight + 5)));

  region_view()->SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kFloating);
  region_view()->DeprecatedLayoutImmediately();
  const gfx::Rect sidebar_bounds =
      region_view()->ahoi_sidebar_tree_view()->bounds();
  const gfx::Rect resize_bounds =
      region_view()->resize_area_for_testing()->bounds();
  EXPECT_EQ(sidebar_bounds.y(), resize_bounds.y());
  EXPECT_EQ(sidebar_bounds.bottom(), resize_bounds.bottom());
  const int floating_card_right = sidebar_bounds.right() - 1;
  EXPECT_TRUE(region_view()->IsPositionInWindowCaption(
      gfx::Point(floating_card_right, sidebar_bounds.y() - 1)));
  EXPECT_TRUE(region_view()->IsPositionInWindowCaption(
      gfx::Point(floating_card_right, sidebar_bounds.bottom() + 1)));
  EXPECT_FALSE(region_view()->IsPositionInWindowCaption(
      gfx::Point(20, ahoi::visual_style::kFloatingSidebarOuterInset +
                         ahoi::visual_style::kSidebarTopInset + 1)));

  std::unique_ptr<views::View> dummy_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::move(previous_sidebar));
  EXPECT_TRUE(dummy_sidebar);
}

IN_PROC_BROWSER_TEST_F(VerticalTabStripRegionViewTest,
                       AhoiNavigationPaintsAboveFloatingSidebar) {
  BrowserView& browser_view = browser()->GetBrowserView();
  ASSERT_TRUE(browser_view.top_container()->layer());
  const auto expect_paint_order = [&]() {
    const std::optional<size_t> sidebar_index =
        browser_view.GetIndexOf(region_view());
    const std::optional<size_t> navigation_index =
        browser_view.GetIndexOf(browser_view.top_container());
    const std::optional<size_t> find_bar_host_index =
        browser_view.GetIndexOf(browser_view.find_bar_host_view());
    const std::optional<size_t> window_scrim_index =
        browser_view.GetIndexOf(browser_view.window_scrim_view());

    ASSERT_TRUE(sidebar_index.has_value());
    ASSERT_TRUE(navigation_index.has_value());
    ASSERT_TRUE(find_bar_host_index.has_value());
    ASSERT_TRUE(window_scrim_index.has_value());
    EXPECT_GT(*navigation_index, *sidebar_index);
    EXPECT_GT(*find_bar_host_index, *navigation_index);
    EXPECT_GT(*window_scrim_index, *navigation_index);
  };
  expect_paint_order();

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
  PrefService* const prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetInteger(
      ahoi::appearance::kFloatingNavigationAutoHideDelayMsPref,
      ahoi::appearance::kMinimumFloatingNavigationAutoHideDelayMs);
  prefs->SetBoolean(ahoi::appearance::kFloatingNavigationAutoHideEnabledPref,
                    true);
  ASSERT_TRUE(browser_view.GetFocusManager());
  browser_view.GetFocusManager()->ClearFocus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !browser_view.top_container()->GetVisible(); }));
  EXPECT_FLOAT_EQ(0.0f, browser_view.top_container()->layer()->opacity());

  std::vector<views::View*> children_before;
  for (views::View* const child : browser_view.children()) {
    children_before.push_back(child);
  }
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  EXPECT_TRUE(browser_view.IsFullscreen());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser_view.top_container()->GetVisible() &&
           browser_view.top_container()->layer()->opacity() == 1.0f;
  }));
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  EXPECT_FALSE(browser_view.IsFullscreen());
  std::vector<views::View*> children_after;
  for (views::View* const child : browser_view.children()) {
    children_after.push_back(child);
  }
  EXPECT_EQ(children_before, children_after);
  ASSERT_TRUE(browser_view.top_container()->layer());
  const bool auto_hide_enabled = prefs->GetBoolean(
      ahoi::appearance::kFloatingNavigationAutoHideEnabledPref);
  prefs->SetBoolean(ahoi::appearance::kFloatingNavigationAutoHideEnabledPref,
                    !auto_hide_enabled);
  ASSERT_TRUE(browser_view.top_container()->layer());
  prefs->SetBoolean(ahoi::appearance::kFloatingNavigationAutoHideEnabledPref,
                    auto_hide_enabled);
  expect_paint_order();
#endif
}

IN_PROC_BROWSER_TEST_F(VerticalTabStripRegionViewTest,
                       AhoiFloatingSidebarOwnsItsNativeDragRoute) {
  BrowserView& browser_view = browser()->GetBrowserView();
  views::View* const sidebar = region_view()->ahoi_sidebar_tree_view();
  ASSERT_TRUE(sidebar);
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      ahoi::appearance::kFloatingNavigationAutoHideEnabledPref, false);
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kDocked));

  views::View* drag_source = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view.DeprecatedLayoutImmediately();
    drag_source = FindDraggableDescendant(sidebar);
    return drag_source != nullptr;
  }));
  ASSERT_TRUE(drag_source->drag_controller());
  const gfx::Point press = drag_source->GetLocalBounds().CenterPoint();
  ui::OSExchangeData drag_data;
  drag_source->drag_controller()->WriteDragDataForView(drag_source, press,
                                                       &drag_data);
  ASSERT_TRUE(ahoi::sidebar::IsBrowserSidebarDragActive(sidebar));
  ASSERT_TRUE(ahoi::sidebar::IsAnyBrowserSidebarDragActive());
  browser_view.DeprecatedLayoutImmediately();

  views::View* const root = browser_view.GetWidget()->GetRootView();
  ASSERT_TRUE(root);
  views::DropHelper drop_helper(root);
  views::View* const new_group_target =
      FindAcceptingDropDescendant(sidebar, drag_data);
  ASSERT_TRUE(new_group_target);
  ASSERT_NE(sidebar, new_group_target);
  views::View* const workspace_selector_host = new_group_target->parent();
  ASSERT_TRUE(workspace_selector_host);
  ASSERT_EQ(2u, workspace_selector_host->children().size());
  views::View* const workspace_selector =
      workspace_selector_host->children().front();
  EXPECT_EQ(workspace_selector->bounds(), new_group_target->bounds());

  gfx::Point docked_new_group_point =
      new_group_target->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToTarget(new_group_target, root,
                                    &docked_new_group_point);
  EXPECT_EQ(new_group_target,
            drop_helper.CalculateTargetView(docked_new_group_point, drag_data,
                                            /*check_can_drop=*/true));

  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kFloating));
  browser_view.DeprecatedLayoutImmediately();
  const gfx::Rect sidebar_bounds = BoundsInTarget(sidebar, root);
  const gfx::Rect new_group_bounds = BoundsInTarget(new_group_target, root);
  const gfx::Rect top_container_bounds =
      BoundsInTarget(browser_view.top_container(), root);
  EXPECT_EQ(BoundsInTarget(workspace_selector, root), new_group_bounds);
  const gfx::Point floating_new_group_point = new_group_bounds.CenterPoint();
  EXPECT_TRUE(new_group_bounds.Contains(floating_new_group_point));

  // New Group overlays the fixed workspace selector during the drag. It
  // remains the deepest semantic target without inserting a sidebar row or
  // moving the saved/open tab surfaces.
  EXPECT_TRUE(IsInsideOrEqual(
      sidebar, root->GetEventHandlerForPoint(floating_new_group_point)));
  EXPECT_EQ(new_group_target,
            drop_helper.CalculateTargetView(floating_new_group_point, drag_data,
                                            /*check_can_drop=*/true));

  // The address bar paints above the card, but once a sidebar drag is active
  // its transparent overlap must route to the sidebar rather than to the
  // BrowserView content/split target below it.
  const gfx::Rect floating_chrome_overlap =
      gfx::IntersectRects(top_container_bounds, sidebar_bounds);
  ASSERT_FALSE(floating_chrome_overlap.IsEmpty());
  const gfx::Point floating_chrome_padding(
      sidebar_bounds.x() + 1, floating_chrome_overlap.CenterPoint().y());
  EXPECT_TRUE(top_container_bounds.Contains(floating_chrome_padding));
  EXPECT_TRUE(sidebar_bounds.Contains(floating_chrome_padding));
  EXPECT_TRUE(IsInsideOrEqual(
      sidebar, root->GetEventHandlerForPoint(floating_chrome_padding)));
  EXPECT_EQ(sidebar,
            drop_helper.CalculateTargetView(floating_chrome_padding, drag_data,
                                            /*check_can_drop=*/true));

  const gfx::Point row_point = [&]() {
    gfx::Point point = drag_source->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToTarget(drag_source, root, &point);
    return point;
  }();
  EXPECT_TRUE(
      IsInsideOrEqual(sidebar, root->GetEventHandlerForPoint(row_point)));
  EXPECT_TRUE(IsInsideOrEqual(
      sidebar, drop_helper.CalculateTargetView(row_point, drag_data,
                                               /*check_can_drop=*/true)));

  // A non-semantic padding gap belongs to the sidebar routing shield. It must
  // never bubble to BrowserView and become a split target for content that is
  // merely underneath the floating card.
  const gfx::Point sidebar_gap(sidebar_bounds.x() + 1,
                               sidebar_bounds.CenterPoint().y());
  EXPECT_EQ(sidebar, drop_helper.CalculateTargetView(sidebar_gap, drag_data,
                                                     /*check_can_drop=*/true));

  const gfx::Rect content_bounds =
      BoundsInTarget(browser_view.multi_contents_view(), root);
  gfx::Point content_point = content_bounds.CenterPoint();
  if (sidebar_bounds.Contains(content_point)) {
    content_point.set_x(content_bounds.right() - 2);
  }
  EXPECT_EQ(&browser_view,
            drop_helper.CalculateTargetView(content_point, drag_data,
                                            /*check_can_drop=*/true));

  drag_source->OnDragDone();
  EXPECT_FALSE(ahoi::sidebar::IsBrowserSidebarDragActive(sidebar));
  EXPECT_FALSE(ahoi::sidebar::IsAnyBrowserSidebarDragActive());
}

IN_PROC_BROWSER_TEST_F(
    VerticalTabStripRegionViewTest,
    AhoiFloatingSidebarTransparentTopChromeDoesNotStealInput) {
  BrowserView& browser_view = browser()->GetBrowserView();
  views::View* const sidebar = region_view()->ahoi_sidebar_tree_view();
  views::View* const top_container = browser_view.top_container();
  views::View* const root = browser_view.GetWidget()->GetRootView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(top_container);
  ASSERT_TRUE(root);
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      ahoi::appearance::kFloatingNavigationAutoHideEnabledPref, false);
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kFloating));
  browser_view.DeprecatedLayoutImmediately();

  const gfx::Rect overlap = gfx::IntersectRects(
      BoundsInTarget(sidebar, root), BoundsInTarget(top_container, root));
  ASSERT_FALSE(overlap.IsEmpty());

  std::optional<gfx::Point> transparent_overlap;
  std::optional<gfx::Point> semantic_chrome_overlap;
  for (int y = overlap.y(); y < overlap.bottom(); ++y) {
    for (int x = overlap.x(); x < overlap.right(); ++x) {
      const gfx::Point root_point(x, y);
      gfx::Point sidebar_point = root_point;
      views::View::ConvertPointToTarget(root, sidebar, &sidebar_point);
      if (!sidebar->HitTestPoint(sidebar_point)) {
        continue;
      }

      gfx::Point top_point = root_point;
      views::View::ConvertPointToTarget(root, top_container, &top_point);
      const bool hits_semantic_chrome = std::ranges::any_of(
          top_container->children(), [&](views::View* child) {
            if (!child->GetVisible() ||
                !child->GetCanProcessEventsWithinSubtree()) {
              return false;
            }
            gfx::Point child_point = top_point;
            views::View::ConvertPointToTarget(top_container, child,
                                              &child_point);
            return child->HitTestPoint(child_point);
          });
      if (hits_semantic_chrome && !semantic_chrome_overlap.has_value()) {
        semantic_chrome_overlap = root_point;
      } else if (!hits_semantic_chrome && !transparent_overlap.has_value()) {
        transparent_overlap = root_point;
      }
      if (transparent_overlap.has_value() &&
          semantic_chrome_overlap.has_value()) {
        break;
      }
    }
    if (transparent_overlap.has_value() &&
        semantic_chrome_overlap.has_value()) {
      break;
    }
  }

  ASSERT_TRUE(transparent_overlap.has_value());
  EXPECT_TRUE(IsInsideOrEqual(
      sidebar, root->GetEventHandlerForPoint(*transparent_overlap)));
  ASSERT_TRUE(semantic_chrome_overlap.has_value());
  EXPECT_TRUE(IsInsideOrEqual(
      top_container, root->GetEventHandlerForPoint(*semantic_chrome_overlap)));
}

IN_PROC_BROWSER_TEST_F(VerticalTabStripRegionViewTest,
                       AhoiSidebarHideAndRevealUseCompositorMotion) {
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);

  // Exercise the production host before replacing it. Floating clipping must
  // not depend on a test-only SetPaintToLayer() call.
  ASSERT_TRUE(region_view()->ahoi_sidebar_tree_view());
  ASSERT_TRUE(region_view()->ahoi_sidebar_tree_view()->layer());

  auto layered_sidebar = std::make_unique<views::View>();
  std::unique_ptr<views::View> previous_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::move(layered_sidebar));
  views::View* const sidebar = region_view()->ahoi_sidebar_tree_view();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(sidebar->layer());
  EXPECT_TRUE(sidebar->GetVisible());

  browser()->GetBrowserView().DeprecatedLayoutImmediately();
  const gfx::Insets docked_margins = *sidebar->GetProperty(views::kMarginsKey);
  EXPECT_EQ(ahoi::visual_style::kSidebarTitlebarHeight, docked_margins.top());

  region_view()->SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kHidden);
  // The outgoing card stays mounted while fading but immediately stops
  // intercepting clicks intended for the page below it.
  EXPECT_TRUE(sidebar->GetVisible());
  EXPECT_FALSE(region_view()->GetCanProcessEventsWithinSubtree());
  EXPECT_GT(region_view()->GetPreferredSize().width(), 0);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !sidebar->GetVisible(); }));
  EXPECT_EQ(0, region_view()->GetPreferredSize().width());
  EXPECT_FLOAT_EQ(0.0f, region_view()->layer()->opacity());

  region_view()->SetAhoiSidebarEdgeRevealed(true);
  EXPECT_TRUE(sidebar->GetVisible());
  EXPECT_TRUE(region_view()->IsAhoiSidebarEdgeRevealed());
  EXPECT_TRUE(region_view()->IsAhoiSidebarFloating());
  EXPECT_TRUE(region_view()->GetCanProcessEventsWithinSubtree());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return region_view()->layer()->opacity() == 1.0f; }));

  const gfx::Insets* margins = sidebar->GetProperty(views::kMarginsKey);
  ASSERT_TRUE(margins);
  EXPECT_EQ(ahoi::visual_style::kFloatingSidebarLeadingInset, margins->left());
  EXPECT_EQ(margins->left(), margins->top());
  EXPECT_EQ(margins->left(), margins->bottom());
  EXPECT_EQ(ahoi::visual_style::kFloatingSidebarTrailingInset,
            margins->right());
  EXPECT_EQ(
      gfx::RoundedCornersF(ahoi::visual_style::kFloatingSidebarCornerRadius),
      sidebar->layer()->rounded_corner_radii());
  EXPECT_TRUE(sidebar->layer()->is_fast_rounded_corner());
  EXPECT_TRUE(sidebar->layer()->GetMasksToBounds());

  region_view()->SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kFloating);
  EXPECT_TRUE(sidebar->layer()->GetMasksToBounds());

  region_view()->SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kHidden);
  EXPECT_TRUE(region_view()->IsAhoiSidebarFloating());
  ASSERT_TRUE(base::test::RunUntil([&]() { return !sidebar->GetVisible(); }));
  EXPECT_FALSE(region_view()->IsAhoiSidebarFloating());

  region_view()->SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kDocked);
  EXPECT_EQ(docked_margins, *sidebar->GetProperty(views::kMarginsKey));
  EXPECT_TRUE(sidebar->layer()->rounded_corner_radii().IsEmpty());
  EXPECT_FALSE(sidebar->layer()->is_fast_rounded_corner());
  EXPECT_FALSE(sidebar->layer()->GetMasksToBounds());

  std::unique_ptr<views::View> dummy_sidebar =
      region_view()->SetAhoiSidebarTreeView(std::move(previous_sidebar));
  EXPECT_TRUE(dummy_sidebar);
  EXPECT_FLOAT_EQ(1.0f, region_view()->layer()->opacity());
  EXPECT_TRUE(region_view()->GetCanProcessEventsWithinSubtree());
}

class LocationBarViewBrowserTest : public InProcessBrowserTest {
 protected:
  LocationBarView* GetLocationBarView() {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    return browser_view->GetLocationBarView();
  }
};

IN_PROC_BROWSER_TEST_F(LocationBarViewBrowserTest,
                       AhoiCopyUrlTooltipHasNoMenuAccelerator) {
  const std::u16string menu_label = l10n_util::GetStringUTF16(IDS_COPY_URL);
  const std::u16string expected_tooltip = gfx::RemoveAccelerator(menu_label);
  bool found_copy_url_tooltip = false;
  for (views::View* const child : GetLocationBarView()->children()) {
    EXPECT_NE(menu_label, child->GetTooltipText());
    found_copy_url_tooltip |= child->GetTooltipText() == expected_tooltip;
  }
  EXPECT_TRUE(found_copy_url_tooltip);
  EXPECT_EQ(std::u16string::npos, expected_tooltip.find(u'&'));
}

IN_PROC_BROWSER_TEST_F(LocationBarViewBrowserTest,
                       AhoiDeveloperMasterPrefUpdatesButtonsLive) {
  LocationBarView* const location_bar = GetLocationBarView();
  ASSERT_TRUE(location_bar);
  const auto find_button = [location_bar](int tooltip_id) -> views::View* {
    const std::u16string tooltip = l10n_util::GetStringUTF16(tooltip_id);
    for (views::View* const child : location_bar->children()) {
      if (child->GetTooltipText() == tooltip) {
        return child;
      }
    }
    return nullptr;
  };
  views::View* const cookie_button =
      find_button(IDS_AHOI_DEVELOPER_COOKIE_BUTTON_TOOLTIP);
  views::View* const cache_button =
      find_button(IDS_AHOI_DEVELOPER_CACHE_BUTTON_TOOLTIP);
  views::View* const toolkit_button =
      find_button(IDS_AHOI_DEVELOPER_HELPERS_BUTTON_TOOLTIP);
  ASSERT_TRUE(cookie_button);
  ASSERT_TRUE(cache_button);
  ASSERT_TRUE(toolkit_button);

  PrefService* const prefs = browser()->GetProfile()->GetPrefs();
  ASSERT_TRUE(prefs->FindPreference(kAhoiToolkitEnabledPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowCookieButtonPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowCacheButtonPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowToolkitButtonPref));
  prefs->ClearPref(kAhoiShowCookieButtonPref);
  prefs->ClearPref(kAhoiShowCacheButtonPref);
  prefs->ClearPref(kAhoiShowToolkitButtonPref);
  ASSERT_FALSE(prefs->GetBoolean(kAhoiShowCookieButtonPref));
  ASSERT_FALSE(prefs->GetBoolean(kAhoiShowCacheButtonPref));
  ASSERT_TRUE(prefs->GetBoolean(kAhoiShowToolkitButtonPref));
  prefs->SetBoolean(kAhoiToolkitEnabledPref, false);
  EXPECT_FALSE(cookie_button->GetVisible());
  EXPECT_FALSE(cache_button->GetVisible());
  EXPECT_FALSE(toolkit_button->GetVisible());

  // This mutates the same live profile and LocationBarView. The registered
  // master-pref callback must reveal the preselected compact action without a
  // browser restart or a second visibility-pref write.
  prefs->SetBoolean(kAhoiToolkitEnabledPref, true);
  EXPECT_FALSE(cookie_button->GetVisible());
  EXPECT_FALSE(cache_button->GetVisible());
  EXPECT_TRUE(toolkit_button->GetVisible());
}
