// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_presentation_animator.h"

#include <algorithm>
#include <cmath>

#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "ui/gfx/animation/tween.h"

namespace ahoi::sidebar {

SidebarPresentationVisualState CalculateSidebarPresentationVisualState(
    double progress) {
  const double clamped_progress = std::clamp(progress, 0.0, 1.0);
  return {
      .opacity = static_cast<float>(clamped_progress),
      .horizontal_offset = static_cast<int>(
          std::lround((1.0 - clamped_progress) *
                      visual_style::kSidebarPresentationRevealOffset)),
  };
}

SidebarPresentationAnimator::SidebarPresentationAnimator(Observer* observer)
    : observer_(observer) {
  CHECK(observer_);
  animation_.Reset(0.0);
}

SidebarPresentationAnimator::~SidebarPresentationAnimator() = default;

void SidebarPresentationAnimator::Reset(bool visible) {
  target_visible_ = visible;
  animation_.Reset(visible ? 1.0 : 0.0);
}

void SidebarPresentationAnimator::SetVisible(bool visible) {
  if (target_visible_ == visible) {
    return;
  }

  target_visible_ = visible;
  animation_.SetSlideDuration(visible ? visual_style::kSidebarRevealDuration
                                      : visual_style::kSidebarHideDuration);
  animation_.SetTweenType(visible ? gfx::Tween::FAST_OUT_SLOW_IN
                                  : gfx::Tween::FAST_OUT_LINEAR_IN);
  if (visible) {
    animation_.Show();
  } else {
    animation_.Hide();
  }
  NotifyObserver();
}

bool SidebarPresentationAnimator::ShouldKeepSurfaceMounted() const {
  return target_visible_ || animation_.is_animating() ||
         animation_.GetCurrentValue() > 0.0;
}

SidebarPresentationVisualState SidebarPresentationAnimator::visual_state()
    const {
  return CalculateSidebarPresentationVisualState(animation_.GetCurrentValue());
}

void SidebarPresentationAnimator::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &animation_) {
    NotifyObserver();
  }
}

void SidebarPresentationAnimator::AnimationEnded(
    const gfx::Animation* animation) {
  if (animation == &animation_) {
    NotifyObserver();
  }
}

void SidebarPresentationAnimator::AnimationCanceled(
    const gfx::Animation* animation) {
  AnimationEnded(animation);
}

void SidebarPresentationAnimator::NotifyObserver() {
  observer_->OnSidebarPresentationAnimationUpdated();
}

}  // namespace ahoi::sidebar
