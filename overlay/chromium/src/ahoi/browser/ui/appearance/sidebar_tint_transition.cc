// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_tint_transition.h"

#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"

namespace ahoi::appearance {

SidebarTintTransition::SidebarTintTransition(Observer* observer)
    : observer_(observer) {
  CHECK(observer_);
  animation_.Reset(1.0);
}

SidebarTintTransition::~SidebarTintTransition() = default;

void SidebarTintTransition::SetTarget(std::optional<SkColor> target,
                                      bool animate) {
  if (!initialized_ || !animate ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    Reset(target);
    return;
  }
  if (target == target_color_) {
    return;
  }

  const std::optional<SkColor> displayed_color = current_color();
  if (displayed_color == target) {
    Reset(target);
    return;
  }

  start_color_ = displayed_color;
  target_color_ = target;
  resetting_animation_ = true;
  animation_.Reset(0.0);
  resetting_animation_ = false;
  animation_.SetSlideDuration(visual_style::kSidebarTintTransitionDuration);
  animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  animation_.Show();
  NotifyObserver();
}

void SidebarTintTransition::Reset(std::optional<SkColor> target) {
  const bool changed =
      !initialized_ || target != target_color_ || animation_.is_animating();
  initialized_ = true;
  start_color_ = target;
  target_color_ = target;
  resetting_animation_ = true;
  animation_.Reset(1.0);
  resetting_animation_ = false;
  if (changed) {
    NotifyObserver();
  }
}

std::optional<SkColor> SidebarTintTransition::current_color() const {
  if (!initialized_) {
    return std::nullopt;
  }
  if (!animation_.is_animating() || start_color_ == target_color_) {
    return target_color_;
  }
  if (!start_color_.has_value() && !target_color_.has_value()) {
    return std::nullopt;
  }

  // A missing endpoint means a transparent version of the peer color. This
  // preserves hue while fading and avoids an unintended muddy-black midpoint.
  const SkColor start =
      start_color_.value_or(SkColorSetA(*target_color_, SK_AlphaTRANSPARENT));
  const SkColor target =
      target_color_.value_or(SkColorSetA(*start_color_, SK_AlphaTRANSPARENT));
  return gfx::Tween::ColorValueBetween(animation_.GetCurrentValue(), start,
                                       target);
}

void SidebarTintTransition::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &animation_) {
    NotifyObserver();
  }
}

void SidebarTintTransition::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &animation_) {
    start_color_ = target_color_;
    NotifyObserver();
  }
}

void SidebarTintTransition::AnimationCanceled(const gfx::Animation* animation) {
  if (resetting_animation_) {
    return;
  }
  AnimationEnded(animation);
}

void SidebarTintTransition::NotifyObserver() {
  observer_->OnSidebarTintTransitionUpdated();
}

}  // namespace ahoi::appearance
