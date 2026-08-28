// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_TINT_TRANSITION_H_
#define AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_TINT_TRANSITION_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"

namespace ahoi::appearance {

// Owns the interruptible visual interpolation between page-derived sidebar
// tints. Domain state remains with the host; this class only supplies the
// color for the current animation frame.
class SidebarTintTransition final : public gfx::AnimationDelegate {
 public:
  class Observer {
   public:
    virtual void OnSidebarTintTransitionUpdated() = 0;

   protected:
    virtual ~Observer() = default;
  };

  explicit SidebarTintTransition(Observer* observer);
  SidebarTintTransition(const SidebarTintTransition&) = delete;
  SidebarTintTransition& operator=(const SidebarTintTransition&) = delete;
  ~SidebarTintTransition() override;

  // Retargets from the color currently on screen. Initial state, reduced
  // motion, accessibility-policy changes and teardown should pass false so no
  // stale semantic background is blended through a transition.
  void SetTarget(std::optional<SkColor> target, bool animate);

  // Installs a stable endpoint immediately.
  void Reset(std::optional<SkColor> target);

  std::optional<SkColor> current_color() const;
  std::optional<SkColor> target_color() const { return target_color_; }
  bool is_animating() const { return animation_.is_animating(); }

  gfx::SlideAnimation* animation_for_testing() { return &animation_; }

 private:
  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  void NotifyObserver();

  const raw_ptr<Observer> observer_;
  gfx::SlideAnimation animation_{this};
  std::optional<SkColor> start_color_;
  std::optional<SkColor> target_color_;
  bool initialized_ = false;
  bool resetting_animation_ = false;
};

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_SIDEBAR_TINT_TRANSITION_H_
