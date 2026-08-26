// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_CONTROLLER_H_
#define AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_CONTROLLER_H_

#include <memory>
#include <string>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/shell/navigation_surface_state.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/mouse_watcher.h"
#include "ui/views/view_observer.h"

namespace views {
class Button;
class View;
class Widget;
}  // namespace views

class PrefService;

namespace ahoi {

// Window-local Views adapter for NavigationSurfaceState. The browser layout
// still owns the ToolbarView bounds; this controller owns only presentation,
// reveal hit testing and interaction leases.
class NavigationSurfaceController final : public gfx::AnimationDelegate,
                                          public views::FocusChangeListener,
                                          public views::MouseWatcherListener,
                                          public views::ViewObserver {
 public:
  NavigationSurfaceController(views::View* host,
                              views::View* top_container,
                              views::View* toolbar,
                              std::u16string reveal_accessible_name,
                              PrefService* prefs = nullptr);
  NavigationSurfaceController(const NavigationSurfaceController&) = delete;
  NavigationSurfaceController& operator=(const NavigationSurfaceController&) =
      delete;
  ~NavigationSurfaceController() override;

  // Called after BrowserView's normal layout so the notch tracks the actual
  // content card across resize, sidebar width and split-view changes.
  void Layout(const gfx::Rect& content_card_bounds);

  NavigationSurfaceState::State state_for_testing() const {
    return state_.state();
  }
  views::View* reveal_notch_for_testing() const { return reveal_notch_; }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;
  void OnFocusManagerDestroying(views::FocusManager* focus_manager) override;

  // views::MouseWatcherListener:
  void MouseMovedOutOfHost() override;

  // views::ViewObserver:
  void OnViewAddedToWidget(views::View* observed_view) override;
  void OnViewRemovedFromWidget(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void OnRevealNotchEntered();
  void OnStateChanged(NavigationSurfaceState::State state);
  void ApplyVisibilityFraction(double value);
  void AttachToWidget();
  void DetachFromWidget();
  void StartMouseWatcher();
  void UpdateInteractionReasons(views::View* focused_view);
  void ReleaseInitialPresentationLease();
  void OnAppearancePolicyChanged(const appearance::GlassPolicy& policy);
  void RefreshNavigationPreferences();

  raw_ptr<views::View> host_ = nullptr;
  raw_ptr<views::View> top_container_ = nullptr;
  raw_ptr<views::View> toolbar_ = nullptr;
  raw_ptr<views::View> reveal_notch_ = nullptr;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  raw_ptr<PrefService> prefs_ = nullptr;
  std::unique_ptr<views::MouseWatcher> mouse_watcher_;
  NavigationSurfaceState state_;
  std::unique_ptr<appearance::AppearanceRuntimeSignalSource>
      appearance_signals_;
  appearance::FloatingNavigationPreferences navigation_preferences_;
  PrefChangeRegistrar navigation_pref_change_registrar_;
  bool reduced_motion_ = false;
  gfx::SlideAnimation visibility_animation_{this};
  base::OneShotTimer initial_presentation_timer_;
  base::ScopedObservation<views::View, views::ViewObserver> host_observation_{
      this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_SHELL_NAVIGATION_SURFACE_CONTROLLER_H_
