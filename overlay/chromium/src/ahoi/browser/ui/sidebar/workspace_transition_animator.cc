// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/workspace_transition_animator.h"

#include "ahoi/browser/ui/visual_style.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
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
  if (!sidebar_layer ||
      (!reduced_motion && !gfx::Animation::ShouldRenderRichAnimation())) {
    return;
  }

  sidebar_layer_ = sidebar_layer->AsWeakPtr();
  sidebar_resting_transform_ = sidebar_layer->transform();
  sidebar_resting_opacity_ = sidebar_layer->opacity();
  sidebar_fades_ = reduced_motion;
  const WorkspaceTransitionVisualState initial_state =
      CalculateWorkspaceTransitionInitialState(direction);
  if (sidebar_fades_) {
    AnimateContentsLayer(sidebar_layer_.get(), initial_state,
                         sidebar_resting_opacity_);
  } else {
    AnimateSidebarLayer(sidebar_layer_.get(), initial_state);
  }
  if (fade_contents && contents_layer && contents_layer != sidebar_layer) {
    contents_layer_ = contents_layer->AsWeakPtr();
    contents_resting_opacity_ = contents_layer->opacity();
    AnimateContentsLayer(contents_layer_.get(), initial_state,
                         contents_resting_opacity_);
  }
}

void WorkspaceTransitionAnimator::Cancel() {
  if (sidebar_layer_) {
    sidebar_layer_->GetAnimator()->StopAnimatingProperty(
        sidebar_fades_ ? ui::LayerAnimationElement::OPACITY
                       : ui::LayerAnimationElement::TRANSFORM);
    if (sidebar_layer_ && sidebar_fades_) {
      sidebar_layer_->SetOpacity(sidebar_resting_opacity_);
    } else if (sidebar_layer_) {
      sidebar_layer_->SetTransform(sidebar_resting_transform_);
    }
  }
  if (contents_layer_) {
    contents_layer_->GetAnimator()->StopAnimatingProperty(
        ui::LayerAnimationElement::OPACITY);
    if (contents_layer_) {
      contents_layer_->SetOpacity(contents_resting_opacity_);
    }
  }
  sidebar_layer_.reset();
  contents_layer_.reset();
}

bool WorkspaceTransitionAnimator::is_animating() const {
  return (sidebar_layer_ &&
          sidebar_layer_->GetAnimator()->IsAnimatingProperty(
              sidebar_fades_ ? ui::LayerAnimationElement::OPACITY
                             : ui::LayerAnimationElement::TRANSFORM)) ||
         (contents_layer_ &&
          contents_layer_->GetAnimator()->IsAnimatingProperty(
              ui::LayerAnimationElement::OPACITY));
}

void WorkspaceTransitionAnimator::AnimateSidebarLayer(
    ui::Layer* layer,
    const WorkspaceTransitionVisualState& initial_state) {
  gfx::Transform initial_transform = sidebar_resting_transform_;
  initial_transform.Translate(initial_state.horizontal_offset, 0);
  layer->SetTransform(initial_transform);

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTransitionDuration(gfx::Animation::RichAnimationDuration(
      visual_style::kWorkspaceTransitionDuration));
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  settings.CacheRenderSurface();
  layer->SetTransform(sidebar_resting_transform_);
}

void WorkspaceTransitionAnimator::AnimateContentsLayer(
    ui::Layer* layer,
    const WorkspaceTransitionVisualState& initial_state,
    float resting_opacity) {
  layer->SetOpacity(initial_state.opacity * resting_opacity);

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  // Reduce Motion disables rich spatial animation on macOS. This deliberate
  // opacity-only fallback must not be collapsed to zero by that same gate.
  settings.SetTransitionDuration(sidebar_fades_
                                     ? visual_style::kModalFadeInDuration
                                     : gfx::Animation::RichAnimationDuration(
                                           visual_style::kModalFadeInDuration));
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  settings.CacheRenderSurface();
  layer->SetOpacity(resting_opacity);
}

}  // namespace ahoi::sidebar
