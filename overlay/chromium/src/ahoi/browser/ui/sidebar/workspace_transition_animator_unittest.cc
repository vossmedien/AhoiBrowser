// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/workspace_transition_animator.h"

#include "ahoi/browser/ui/visual_style.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"

namespace ahoi::sidebar {
namespace {

TEST(WorkspaceTransitionAnimatorTest, InitialStateTracksVisualDirection) {
  const WorkspaceTransitionVisualState next =
      CalculateWorkspaceTransitionInitialState(
          WorkspaceTransitionDirection::kNext);
  EXPECT_FLOAT_EQ(visual_style::kWorkspaceTransitionInitialOpacity,
                  next.opacity);
  EXPECT_EQ(visual_style::kWorkspaceTransitionOffset, next.horizontal_offset);

  const WorkspaceTransitionVisualState previous =
      CalculateWorkspaceTransitionInitialState(
          WorkspaceTransitionDirection::kPrevious);
  EXPECT_FLOAT_EQ(next.opacity, previous.opacity);
  EXPECT_EQ(-next.horizontal_offset, previous.horizontal_offset);
}

TEST(WorkspaceTransitionAnimatorTest, CancelNormalizesBothCommittedSurfaces) {
  base::test::TaskEnvironment task_environment;
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  auto sidebar_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  auto contents_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  WorkspaceTransitionAnimator animator;

  animator.Start(sidebar_layer.get(), contents_layer.get(),
                 WorkspaceTransitionDirection::kNext,
                 /*reduced_motion=*/false);
  EXPECT_TRUE(animator.is_animating());

  animator.Cancel();
  EXPECT_FALSE(animator.is_animating());
  EXPECT_FLOAT_EQ(1.0f, sidebar_layer->opacity());
  EXPECT_FLOAT_EQ(1.0f, contents_layer->opacity());
  EXPECT_TRUE(sidebar_layer->transform().IsIdentity());
  EXPECT_TRUE(contents_layer->transform().IsIdentity());
}

TEST(WorkspaceTransitionAnimatorTest, ReducedMotionNeverStartsAnimation) {
  auto sidebar_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  auto contents_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  WorkspaceTransitionAnimator animator;

  animator.Start(sidebar_layer.get(), contents_layer.get(),
                 WorkspaceTransitionDirection::kPrevious,
                 /*reduced_motion=*/true);

  EXPECT_FALSE(animator.is_animating());
  EXPECT_FLOAT_EQ(1.0f, sidebar_layer->opacity());
  EXPECT_FLOAT_EQ(1.0f, contents_layer->opacity());
  EXPECT_TRUE(sidebar_layer->transform().IsIdentity());
  EXPECT_TRUE(contents_layer->transform().IsIdentity());
}

TEST(WorkspaceTransitionAnimatorTest,
     DestroyedSurfaceIsIgnoredWhenTransitionIsCancelled) {
  base::test::TaskEnvironment task_environment;
  gfx::ScopedAnimationDurationScaleMode duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  const auto render_mode = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  ASSERT_TRUE(render_mode);
  auto sidebar_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  auto contents_layer = ui::Layer::Create(ui::LAYER_NOT_DRAWN);
  WorkspaceTransitionAnimator animator;

  animator.Start(sidebar_layer.get(), contents_layer.get(),
                 WorkspaceTransitionDirection::kNext,
                 /*reduced_motion=*/false);
  contents_layer.reset();

  animator.Cancel();
  EXPECT_FALSE(animator.is_animating());
  EXPECT_FLOAT_EQ(1.0f, sidebar_layer->opacity());
  EXPECT_TRUE(sidebar_layer->transform().IsIdentity());
}

}  // namespace
}  // namespace ahoi::sidebar
