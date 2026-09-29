// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_surface_controller.h"

#include <memory>

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "cc/trees/layer_tree_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/test/draw_waiter_for_test.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {
namespace {

class NavigationSurfaceControllerTest : public views::ViewsTestBase {};

TEST_F(NavigationSurfaceControllerTest,
       OnlyToolbarAnchoredNonModalBubbleKeepsNavigationVisible) {
  views::View root;
  views::View* const toolbar =
      root.AddChildView(std::make_unique<views::View>());
  views::View* const page = root.AddChildView(std::make_unique<views::View>());
  views::View* const toolbar_anchor =
      toolbar->AddChildView(std::make_unique<views::View>());
  views::View* const page_anchor =
      page->AddChildView(std::make_unique<views::View>());

  EXPECT_TRUE(
      IsToolbarAnchoredNavigationBubble(false, toolbar_anchor, toolbar));
  EXPECT_FALSE(IsToolbarAnchoredNavigationBubble(false, page_anchor, toolbar));
  EXPECT_FALSE(IsToolbarAnchoredNavigationBubble(false, nullptr, toolbar));
  EXPECT_FALSE(
      IsToolbarAnchoredNavigationBubble(true, toolbar_anchor, toolbar));
}

TEST_F(NavigationSurfaceControllerTest,
       NativeBackgroundAndBorderAreNotReplacedByMaterialUpdates) {
  views::View root;
  views::View* const top_container =
      root.AddChildView(std::make_unique<views::View>());
  views::View* const toolbar =
      top_container->AddChildView(std::make_unique<views::View>());
  toolbar->SetBackground(views::CreateSolidBackground(ui::kColorSysSurface));
  toolbar->SetBorder(views::CreateEmptyBorder(3));
  const auto* const background = toolbar->background();
  const auto* const border = toolbar->GetBorder();
  int background_updates = 0;
  appearance::SurfaceAppearance last_surface;
  NavigationSurfaceController controller(
      &root, top_container, toolbar, u"Reveal navigation", nullptr,
      base::BindRepeating(
          [](int* updates, appearance::SurfaceAppearance* last,
             const appearance::SurfaceAppearance& surface) {
            ++*updates;
            *last = surface;
          },
          &background_updates, &last_surface));
  EXPECT_GT(background_updates, 0);
  EXPECT_EQ(background, toolbar->background());
  EXPECT_EQ(border, toolbar->GetBorder());
  EXPECT_FALSE(toolbar->layer()->fills_bounds_opaquely());
  EXPECT_EQ(gfx::RoundedCornersF(last_surface.corner_radius),
            toolbar->layer()->rounded_corner_radii());

  const int updates_before_layout = background_updates;
  controller.Layout(gfx::Rect(0, 0, 800, 600));
  EXPECT_GT(background_updates, updates_before_layout);
  EXPECT_EQ(background, toolbar->background());
  EXPECT_EQ(border, toolbar->GetBorder());

  controller.SetFullscreenActive(true);
  EXPECT_EQ(background, toolbar->background());
  EXPECT_EQ(border, toolbar->GetBorder());
  EXPECT_FALSE(toolbar->layer()->fills_bounds_opaquely());
  EXPECT_TRUE(toolbar->layer()->rounded_corner_radii().IsEmpty());
  EXPECT_FLOAT_EQ(0.0f, toolbar->layer()->background_blur());
  EXPECT_FALSE(last_surface.uses_glass());
  EXPECT_FLOAT_EQ(1.0f, last_surface.opacity);

  controller.SetFullscreenActive(false);
  EXPECT_EQ(background, toolbar->background());
  EXPECT_EQ(border, toolbar->GetBorder());
  EXPECT_GT(last_surface.corner_radius, 0);
  EXPECT_EQ(gfx::RoundedCornersF(last_surface.corner_radius),
            toolbar->layer()->rounded_corner_radii());
}

TEST_F(NavigationSurfaceControllerTest,
       RoundedSurfaceOutputMatchesPaintInGlassAndOpaqueFallback) {
  views::View surface_view;
  surface_view.SetBounds(0, 0, 220, 80);
  for (bool glass_enabled : {true, false}) {
    appearance::GlassPolicy policy;
    policy.enabled = glass_enabled;
    const auto surface = appearance::AppearanceResolver::Resolve(
        appearance::SurfaceRole::kFloatingNavigation, policy);
    appearance::ApplySurfaceAppearance(&surface_view, surface);

    ASSERT_TRUE(surface_view.layer());
    const gfx::RoundedCornersF expected_radii(surface.corner_radius);
    ASSERT_TRUE(surface_view.background()->GetRoundedCornerRadii());
    EXPECT_EQ(expected_radii,
              *surface_view.background()->GetRoundedCornerRadii());
    EXPECT_EQ(expected_radii, surface_view.layer()->rounded_corner_radii());
    EXPECT_TRUE(surface_view.layer()->is_fast_rounded_corner());
    EXPECT_FALSE(surface_view.layer()->fills_bounds_opaquely());
    EXPECT_FALSE(surface_view.layer()->GetMasksToBounds());
    EXPECT_FLOAT_EQ(glass_enabled ? surface.background_blur_sigma : 0.0f,
                    surface_view.layer()->background_blur());
    surface_view.SetSize(gfx::Size(140, 60));
    EXPECT_EQ(expected_radii, surface_view.layer()->rounded_corner_radii());
  }
}

TEST_F(NavigationSurfaceControllerTest,
       ReapplyingIdenticalLayerMaterialDoesNotRequestAnotherFrame) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* const view = widget->SetContentsView(std::make_unique<views::View>());
  view->SetPaintToLayer();
  widget->SetBounds(gfx::Rect(0, 0, 220, 80));
  widget->Show();
  auto* const compositor = widget->GetCompositor();
  ASSERT_TRUE(compositor);
  appearance::GlassPolicy policy;
  policy.enabled = false;
  auto surface = appearance::AppearanceResolver::Resolve(
      appearance::SurfaceRole::kFloatingNavigation, policy);

  for (int radius : {14, 0}) {
    surface.corner_radius = radius;
    appearance::ApplySurfaceLayerAppearance(view->layer(), surface);
    ui::DrawWaiterForTest::WaitForCommit(compositor);
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return !compositor->host_for_testing()->CommitRequested(); }));
    EXPECT_EQ(radius != 0, view->layer()->is_fast_rounded_corner());
    for (int repeated_layout = 0; repeated_layout < 4; ++repeated_layout) {
      appearance::ApplySurfaceLayerAppearance(view->layer(), surface);
      EXPECT_FALSE(compositor->host_for_testing()->CommitRequested());
    }
  }
}

TEST_F(NavigationSurfaceControllerTest,
       CallerOwnedCornersAndClipSurviveMaterialChangesAndClear) {
  views::View surface_view;
  surface_view.SetPaintToLayer();
  const gfx::Rect clip(1, 2, 130, 60);
  gfx::Transform transform;
  transform.Translate(7, 3);
  surface_view.layer()->SetTransform(transform);
  surface_view.layer()->SetMasksToBounds(true);
  surface_view.layer()->SetClipRect(clip);
  const auto surface = appearance::AppearanceResolver::Resolve(
      appearance::SurfaceRole::kSidebar, {});

  // The parent decides square docked vs rounded floating geometry, not the
  // sidebar's material role. Policy changes must not undo either decision.
  for (int corner_radius : {0, 14}) {
    const gfx::RoundedCornersF caller_radii(corner_radius);
    surface_view.layer()->SetRoundedCornerRadius(caller_radii);
    surface_view.layer()->SetIsFastRoundedCorner(corner_radius > 0);
    appearance::ApplySurfaceAppearance(
        &surface_view, surface, appearance::SurfaceCornerOwnership::kCaller);
    EXPECT_EQ(caller_radii, surface_view.layer()->rounded_corner_radii());
    EXPECT_EQ(corner_radius > 0,
              surface_view.layer()->is_fast_rounded_corner());
    appearance::ClearSurfaceBackgroundAppearance(
        &surface_view, appearance::SurfaceCornerOwnership::kCaller);
    EXPECT_EQ(caller_radii, surface_view.layer()->rounded_corner_radii());
    EXPECT_EQ(transform, surface_view.layer()->transform());
    EXPECT_EQ(clip, surface_view.layer()->clip_rect());
    EXPECT_TRUE(surface_view.layer()->GetMasksToBounds());
  }
}

TEST_F(NavigationSurfaceControllerTest,
       MovingSurfaceToDialogClientClearsOnlyOwnedMaterialProperties) {
  views::View former_host;
  former_host.SetBorder(views::CreateEmptyBorder(4));
  const auto* const border = former_host.GetBorder();
  const auto surface = appearance::AppearanceResolver::Resolve(
      appearance::SurfaceRole::kCommandBar, {});
  appearance::ApplySurfaceBackgroundAppearance(&former_host, surface);
  gfx::Transform transform;
  transform.Translate(5, 9);
  former_host.layer()->SetTransform(transform);
  const gfx::Rect clip(2, 3, 80, 30);
  former_host.layer()->SetClipRect(clip);

  appearance::ClearSurfaceBackgroundAppearance(&former_host);
  EXPECT_EQ(nullptr, former_host.background());
  EXPECT_EQ(border, former_host.GetBorder());
  EXPECT_TRUE(former_host.layer()->rounded_corner_radii().IsEmpty());
  EXPECT_FALSE(former_host.layer()->is_fast_rounded_corner());
  EXPECT_FALSE(former_host.layer()->fills_bounds_opaquely());
  EXPECT_FLOAT_EQ(0.0f, former_host.layer()->background_blur());
  EXPECT_EQ(transform, former_host.layer()->transform());
  EXPECT_EQ(clip, former_host.layer()->clip_rect());
}

}  // namespace
}  // namespace ahoi
