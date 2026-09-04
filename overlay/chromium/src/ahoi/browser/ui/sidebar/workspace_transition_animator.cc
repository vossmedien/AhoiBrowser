// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/workspace_transition_animator.h"

#include "ahoi/browser/ui/visual_style.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform.h"

namespace ahoi::sidebar {

WorkspaceTransitionVisualState CalculateWorkspaceTransitionInitialState(
    WorkspaceTransitionDirection direction) {
  return {
      .opacity = visual_style::kWorkspaceTransitionInitialOpacity,
      .horizontal_offset = direction == WorkspaceTransitionDirection::kNext
                               ? visual_style::kWorkspaceTransitionOffset
                               : -visual_style::kWorkspaceTransitionOffset,
  };
}

WorkspaceTransitionAnimator::WorkspaceTransitionAnimator() = default;

WorkspaceTransitionAnimator::~WorkspaceTransitionAnimator() {
  Cancel();
}

void WorkspaceTransitionAnimator::Start(ui::Layer* sidebar_layer,
                                        ui::Layer* contents_layer,
                                        WorkspaceTransitionDirection direction,
                                        bool fade_contents,
                                        bool reduced_motion) {
  Cancel();
  if (!sidebar_layer || reduced_motion ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }

  sidebar_layer_ = sidebar_layer->AsWeakPtr();
  const WorkspaceTransitionVisualState initial_state =
      CalculateWorkspaceTransitionInitialState(direction);
  AnimateSidebarLayer(sidebar_layer_.get(), initial_state);
  if (fade_contents && contents_layer && contents_layer != sidebar_layer) {
    contents_layer_ = contents_layer->AsWeakPtr();
    AnimateContentsLayer(contents_layer_.get(), initial_state);
  }
}

void WorkspaceTransitionAnimator::Cancel() {
  ResetLayer(sidebar_layer_.get());
  ResetLayer(contents_layer_.get());
  sidebar_layer_.reset();
  contents_layer_.reset();
}

bool WorkspaceTransitionAnimator::is_animating() const {
  return (sidebar_layer_ && sidebar_layer_->GetAnimator()->is_animating()) ||
         (contents_layer_ && contents_layer_->GetAnimator()->is_animating());
}

void WorkspaceTransitionAnimator::ResetLayer(ui::Layer* layer) {
  if (!layer) {
    return;
  }
  layer->GetAnimator()->AbortAllAnimations();
  layer->SetOpacity(1.0f);
  layer->SetTransform(gfx::Transform());
}

void WorkspaceTransitionAnimator::AnimateSidebarLayer(
    ui::Layer* layer,
    const WorkspaceTransitionVisualState& initial_state) {
  gfx::Transform initial_transform;
  initial_transform.Translate(initial_state.horizontal_offset, 0);
  layer->SetOpacity(1.0f);
  layer->SetTransform(initial_transform);

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTransitionDuration(gfx::Animation::RichAnimationDuration(
      visual_style::kWorkspaceTransitionDuration));
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  settings.CacheRenderSurface();
  layer->SetTransform(gfx::Transform());
}

void WorkspaceTransitionAnimator::AnimateContentsLayer(
    ui::Layer* layer,
    const WorkspaceTransitionVisualState& initial_state) {
  layer->SetOpacity(initial_state.opacity);
  layer->SetTransform(gfx::Transform());

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTransitionDuration(gfx::Animation::RichAnimationDuration(
      visual_style::kWorkspaceTransitionDuration));
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  settings.CacheRenderSurface();
  layer->SetOpacity(1.0f);
}

}  // namespace ahoi::sidebar
