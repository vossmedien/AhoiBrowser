// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_presentation_state.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"

namespace {

class LayoutInvalidationCounter final : public views::ViewObserver {
 public:
  explicit LayoutInvalidationCounter(views::View* view) {
    observation_.Observe(view);
  }
  ~LayoutInvalidationCounter() override = default;

  void OnViewLayoutInvalidated(views::View* view) override { ++count_; }
  void OnViewIsDeleting(views::View* view) override { observation_.Reset(); }

  int count() const { return count_; }
  void ResetCount() { count_ = 0; }

 private:
  int count_ = 0;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace

class AhoiSidebarLayoutInvalidationBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 protected:
  VerticalTabStripRegionView* region_view() {
    return browser()
        ->GetBrowserView()
        .vertical_tab_strip_region_view_for_testing();
  }
};

IN_PROC_BROWSER_TEST_F(AhoiSidebarLayoutInvalidationBrowserTest,
                       RepeatedIdenticalToolbarHeightDoesNotInvalidateLayout) {
  auto& browser_view = browser()->GetBrowserView();
  ASSERT_TRUE(browser_view.IsAhoiBrowserSurface());
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kDocked));
  auto* const region = region_view();
  ASSERT_TRUE(region);
  auto* const sidebar = region->ahoi_sidebar_tree_view();
  auto* const native_top_container = region->GetTopContainer();
  auto* const root = browser_view.GetWidget()->GetRootView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(native_top_container);
  ASSERT_TRUE(root);
  constexpr int kHeight = ahoi::visual_style::kSidebarTitlebarHeight;
  region->SetToolbarHeightForLayout(kHeight);
  const auto* const margins = sidebar->GetProperty(views::kMarginsKey);
  ASSERT_TRUE(margins);
  const gfx::Insets original_margins = *margins;
  const bool region_needed_layout = region->needs_layout();
  const bool root_needed_layout = root->needs_layout();
  LayoutInvalidationCounter region_invalidations(region);
  LayoutInvalidationCounter native_invalidations(native_top_container);
  LayoutInvalidationCounter root_invalidations(root);

  // Deliberately do not run an idle loop to settle a potentially self-renewing
  // layout. These synchronous notifications expose the regression directly.
  for (int repeat = 0; repeat < 3; ++repeat) {
    region->SetToolbarHeightForLayout(kHeight);
    native_top_container->SetToolbarHeightForLayout(kHeight);
  }

  EXPECT_EQ(0, region_invalidations.count());
  EXPECT_EQ(0, native_invalidations.count());
  EXPECT_EQ(0, root_invalidations.count());
  EXPECT_EQ(original_margins, *sidebar->GetProperty(views::kMarginsKey));
  EXPECT_EQ(region_needed_layout, region->needs_layout());
  EXPECT_EQ(root_needed_layout, root->needs_layout());
}

IN_PROC_BROWSER_TEST_F(AhoiSidebarLayoutInvalidationBrowserTest,
                       ChangedToolbarHeightUpdatesMarginsAndRequestsLayout) {
  auto& browser_view = browser()->GetBrowserView();
  ASSERT_TRUE(browser_view.IsAhoiBrowserSurface());
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(
      ahoi::sidebar::SidebarPresentationMode::kDocked));
  auto* const region = region_view();
  ASSERT_TRUE(region);
  auto* const sidebar = region->ahoi_sidebar_tree_view();
  auto* const native_top_container = region->GetTopContainer();
  auto* const root = browser_view.GetWidget()->GetRootView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(native_top_container);
  ASSERT_TRUE(root);
  constexpr int kOriginalHeight = ahoi::visual_style::kSidebarTitlebarHeight;
  constexpr int kChangedHeight = kOriginalHeight + 12;
  region->SetToolbarHeightForLayout(kOriginalHeight);
  const auto* const margins = sidebar->GetProperty(views::kMarginsKey);
  ASSERT_TRUE(margins);
  const gfx::Insets original_margins = *margins;
  LayoutInvalidationCounter region_invalidations(region);
  LayoutInvalidationCounter native_invalidations(native_top_container);
  LayoutInvalidationCounter root_invalidations(root);

  region->SetToolbarHeightForLayout(kChangedHeight);
  gfx::Insets changed_margins = original_margins;
  changed_margins.set_top(kChangedHeight);
  EXPECT_EQ(changed_margins, *sidebar->GetProperty(views::kMarginsKey));
  EXPECT_GT(region_invalidations.count(), 0);
  EXPECT_GT(native_invalidations.count(), 0);
  EXPECT_GT(root_invalidations.count(), 0);
  EXPECT_TRUE(region->needs_layout());
  EXPECT_TRUE(root->needs_layout());

  // Exercise the actual native FlexLayout once at the supplied height. A
  // BrowserView-wide layout would deliberately replace it with the product's
  // stable titlebar height, so keep this check scoped to the region itself.
  region->DeprecatedLayoutImmediately();
  EXPECT_EQ(changed_margins, *sidebar->GetProperty(views::kMarginsKey));
  EXPECT_GE(sidebar->y(), kChangedHeight);

  region_invalidations.ResetCount();
  native_invalidations.ResetCount();
  root_invalidations.ResetCount();
  region->SetToolbarHeightForLayout(kChangedHeight);
  EXPECT_EQ(0, region_invalidations.count());
  EXPECT_EQ(0, native_invalidations.count());
  EXPECT_EQ(0, root_invalidations.count());
}

IN_PROC_BROWSER_TEST_F(AhoiSidebarLayoutInvalidationBrowserTest,
                       SameHeightRetainsPresentationSpecificMargins) {
  using Mode = ahoi::sidebar::SidebarPresentationMode;
  auto& browser_view = browser()->GetBrowserView();
  ASSERT_TRUE(browser_view.IsAhoiBrowserSurface());
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kDocked));
  auto* const region = region_view();
  ASSERT_TRUE(region);
  auto* const sidebar = region->ahoi_sidebar_tree_view();
  ASSERT_TRUE(sidebar);
  constexpr int kHeight = ahoi::visual_style::kSidebarTitlebarHeight;
  region->SetToolbarHeightForLayout(kHeight);
  const auto* const margins = sidebar->GetProperty(views::kMarginsKey);
  ASSERT_TRUE(margins);
  const gfx::Insets docked = *margins;
  EXPECT_EQ(kHeight, docked.top());
  EXPECT_EQ(0, docked.left());
  EXPECT_EQ(0, docked.right());
  const gfx::Insets floating =
      gfx::Insets::TLBR(ahoi::visual_style::kFloatingSidebarOuterInset,
                        ahoi::visual_style::kFloatingSidebarLeadingInset,
                        ahoi::visual_style::kFloatingSidebarOuterInset,
                        ahoi::visual_style::kFloatingSidebarTrailingInset);
  const gfx::Insets edge_revealed =
      gfx::Insets::TLBR(ahoi::visual_style::kFloatingSidebarOuterInset,
                        ahoi::visual_style::kContentCardInset,
                        ahoi::visual_style::kFloatingSidebarOuterInset,
                        ahoi::visual_style::kFloatingSidebarTrailingInset);

  // The mode seam changes margins synchronously, independently of the reveal
  // animation's current fraction. No timer or event-loop drain is required.
  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kFloating));
  EXPECT_EQ(floating, *sidebar->GetProperty(views::kMarginsKey));
  region->SetToolbarHeightForLayout(kHeight);
  EXPECT_EQ(floating, *sidebar->GetProperty(views::kMarginsKey));

  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kHidden));
  region->SetAhoiSidebarEdgeRevealed(true);
  EXPECT_EQ(edge_revealed, *sidebar->GetProperty(views::kMarginsKey));
  region->SetToolbarHeightForLayout(kHeight);
  EXPECT_EQ(edge_revealed, *sidebar->GetProperty(views::kMarginsKey));

  ASSERT_TRUE(browser_view.SetAhoiSidebarPresentationMode(Mode::kDocked));
  EXPECT_EQ(docked, *sidebar->GetProperty(views::kMarginsKey));
  region->SetToolbarHeightForLayout(kHeight);
  EXPECT_EQ(docked, *sidebar->GetProperty(views::kMarginsKey));
}
