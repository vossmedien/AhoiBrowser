// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_ANIMATOR_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_ANIMATOR_H_

#include "base/memory/raw_ptr.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"

namespace ahoi::sidebar {

// Compositor-facing state derived from the shared sidebar motion curve. The
// host owns layout and applies this state to its existing layer; this class
// deliberately does not own Views or browser lifetime.
struct SidebarPresentationVisualState {
  float opacity = 0.0f;
  int horizontal_offset = 0;
};

SidebarPresentationVisualState CalculateSidebarPresentationVisualState(
    double progress,
    int horizontal_travel);

// Small reusable motion controller for docked, floating and edge-revealed
// sidebar presentation. Keeping the clock outside BrowserView avoids coupling
// the product animation to Chromium's vertical-tab expansion state machine.
class SidebarPresentationAnimator final : public gfx::AnimationDelegate {
 public:
  class Observer {
   public:
    virtual void OnSidebarPresentationAnimationUpdated() = 0;

   protected:
    virtual ~Observer() = default;
  };

  explicit SidebarPresentationAnimator(Observer* observer);
  SidebarPresentationAnimator(const SidebarPresentationAnimator&) = delete;
  SidebarPresentationAnimator& operator=(const SidebarPresentationAnimator&) =
      delete;
  ~SidebarPresentationAnimator() override;

  // Installs a stable state without motion. Used for first presentation and
  // teardown so startup never flashes and stock Chromium chrome is restored.
  void Reset(bool visible);

  // Reverses cleanly from the current fraction when the user toggles again
  // before the previous transition has completed.
  void SetVisible(bool visible);

  bool target_visible() const { return target_visible_; }
  bool is_animating() const { return animation_.is_animating(); }
  bool ShouldKeepSurfaceMounted() const;
  double visibility_fraction() const;
  SidebarPresentationVisualState visual_state(int horizontal_travel) const;

  gfx::SlideAnimation* animation_for_testing() { return &animation_; }

 private:
  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  void NotifyObserver();

  const raw_ptr<Observer> observer_;
  gfx::SlideAnimation animation_{this};
  bool target_visible_ = false;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_PRESENTATION_ANIMATOR_H_
