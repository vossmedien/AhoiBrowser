// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_presentation_animator.h"

#include "ahoi/browser/ui/visual_style.h"
#include "base/test/gtest_util.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"

namespace ahoi::sidebar {

namespace {

class TestObserver final : public SidebarPresentationAnimator::Observer {
 public:
  void OnSidebarPresentationAnimationUpdated() override { ++update_count; }

  int update_count = 0;
};

TEST(SidebarPresentationAnimatorTest,
     VisualStateClampsAndSlidesFromLeadingEdge) {
  const SidebarPresentationVisualState hidden =
      CalculateSidebarPresentationVisualState(-0.5);
  EXPECT_FLOAT_EQ(0.0f, hidden.opacity);
  EXPECT_EQ(visual_style::kSidebarPresentationRevealOffset,
            hidden.horizontal_offset);

  const SidebarPresentationVisualState midpoint =
      CalculateSidebarPresentationVisualState(0.5);
  EXPECT_FLOAT_EQ(0.5f, midpoint.opacity);
  EXPECT_EQ(visual_style::kSidebarPresentationRevealOffset / 2,
            midpoint.horizontal_offset);

  const SidebarPresentationVisualState visible =
      CalculateSidebarPresentationVisualState(1.5);
  EXPECT_FLOAT_EQ(1.0f, visible.opacity);
  EXPECT_EQ(0, visible.horizontal_offset);
}

TEST(SidebarPresentationAnimatorTest, ResetProvidesStableMountedState) {
  TestObserver observer;
  SidebarPresentationAnimator animator(&observer);

  animator.Reset(false);
  EXPECT_FALSE(animator.target_visible());
  EXPECT_FALSE(animator.is_animating());
  EXPECT_FALSE(animator.ShouldKeepSurfaceMounted());
  EXPECT_FLOAT_EQ(0.0f, animator.visual_state().opacity);

  animator.Reset(true);
  EXPECT_TRUE(animator.target_visible());
  EXPECT_FALSE(animator.is_animating());
  EXPECT_TRUE(animator.ShouldKeepSurfaceMounted());
  EXPECT_FLOAT_EQ(1.0f, animator.visual_state().opacity);
  EXPECT_EQ(0, observer.update_count);
}

TEST(SidebarPresentationAnimatorTest,
     ReversalKeepsSurfaceMountedAndUpdatesTarget) {
  base::test::TaskEnvironment task_environment;
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  TestObserver observer;
  SidebarPresentationAnimator animator(&observer);
  animator.Reset(true);

  animator.SetVisible(false);
  EXPECT_FALSE(animator.target_visible());
  EXPECT_TRUE(animator.is_animating());
  EXPECT_TRUE(animator.ShouldKeepSurfaceMounted());
  EXPECT_GT(observer.update_count, 0);

  animator.SetVisible(true);
  EXPECT_TRUE(animator.target_visible());
  EXPECT_TRUE(animator.is_animating());
  EXPECT_TRUE(animator.ShouldKeepSurfaceMounted());
}

TEST(SidebarPresentationAnimatorTest, RejectsMissingObserver) {
  EXPECT_CHECK_DEATH(SidebarPresentationAnimator(nullptr));
}

}  // namespace

}  // namespace ahoi::sidebar
