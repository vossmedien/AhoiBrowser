// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/shell/navigation_surface_controller.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/mouse_watcher_view_host.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {

namespace {

class NavigationRevealNotchView final : public views::Button {
  METADATA_HEADER(NavigationRevealNotchView, views::Button)

 public:
  NavigationRevealNotchView(PressedCallback pressed_callback,
                            base::RepeatingClosure entered_callback,
                            std::u16string accessible_name)
      : views::Button(std::move(pressed_callback)),
        entered_callback_(std::move(entered_callback)) {
    SetPreferredSize(gfx::Size(visual_style::kNavigationRevealNotchWidth,
                               visual_style::kNavigationRevealNotchHeight));
    constexpr float kBottomRadius =
        visual_style::kNavigationRevealNotchVisualHeight / 2.0f;
    SetBackground(views::CreateRoundedRectBackground(
        visual_style::kSelectedSurface,
        gfx::RoundedCornersF(0.0f, 0.0f, kBottomRadius, kBottomRadius),
        gfx::Insets::TLBR(0, 0,
                          visual_style::kNavigationRevealNotchHeight -
                              visual_style::kNavigationRevealNotchVisualHeight,
                          0)));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetTooltipText(accessible_name);
    GetViewAccessibility().SetName(accessible_name);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    entered_callback_.Run();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    entered_callback_.Run();
  }

 private:
  base::RepeatingClosure entered_callback_;
};

BEGIN_METADATA(NavigationRevealNotchView)
END_METADATA

}  // namespace

NavigationSurfaceController::NavigationSurfaceController(
    views::View* host,
    views::View* top_container,
    views::View* toolbar,
    std::u16string reveal_accessible_name,
    PrefService* prefs)
    : host_(host),
      top_container_(top_container),
      toolbar_(toolbar),
      prefs_(prefs),
      state_(base::BindRepeating(&NavigationSurfaceController::OnStateChanged,
                                 base::Unretained(this)),
             NavigationSurfaceState::Configuration{
                 .auto_hide_delay =
                     visual_style::kNavigationSurfaceAutoHideDelay}) {
  CHECK(host_);
  CHECK(top_container_);
  CHECK(toolbar_);

  top_container_->SetPaintToLayer();
  top_container_->layer()->SetFillsBoundsOpaquely(false);
  auto notch = std::make_unique<NavigationRevealNotchView>(
      base::BindRepeating(&NavigationSurfaceController::OnRevealNotchEntered,
                          base::Unretained(this)),
      base::BindRepeating(&NavigationSurfaceController::OnRevealNotchEntered,
                          base::Unretained(this)),
      std::move(reveal_accessible_name));
  reveal_notch_ = host_->AddChildView(std::move(notch));
  reveal_notch_->SetPaintToLayer();
  reveal_notch_->layer()->SetFillsBoundsOpaquely(false);

  visibility_animation_.SetSlideDuration(
      visual_style::kNavigationSurfaceRevealDuration);
  visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  visibility_animation_.Reset(0.0);
  ApplyVisibilityFraction(0.0);

  appearance_signals_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs_, base::BindRepeating(
                      &NavigationSurfaceController::OnAppearancePolicyChanged,
                      base::Unretained(this)));
  OnAppearancePolicyChanged(appearance_signals_->policy());

  if (prefs_) {
    navigation_pref_change_registrar_.Init(prefs_);
    for (const char* pref_name : {
             appearance::kFloatingNavigationAutoHideEnabledPref,
             appearance::kFloatingNavigationRevealNotchEnabledPref,
             appearance::kFloatingNavigationAutoHideDelayMsPref,
         }) {
      if (prefs_->FindPreference(pref_name)) {
        navigation_pref_change_registrar_.Add(
            pref_name,
            base::BindRepeating(
                &NavigationSurfaceController::RefreshNavigationPreferences,
                base::Unretained(this)));
      }
    }
  }
  RefreshNavigationPreferences();
  host_observation_.Observe(host_);
  if (host_->GetWidget()) {
    AttachToWidget();
  }
}

NavigationSurfaceController::~NavigationSurfaceController() {
  DetachFromWidget();
  host_observation_.Reset();
}

void NavigationSurfaceController::Layout(const gfx::Rect& content_card_bounds) {
  if (!reveal_notch_) {
    return;
  }
  const int width = std::min(visual_style::kNavigationRevealNotchWidth,
                             content_card_bounds.width());
  const int x = content_card_bounds.x() +
                std::max(0, (content_card_bounds.width() - width) / 2);
  reveal_notch_->SetBounds(x, content_card_bounds.y(), width,
                           visual_style::kNavigationRevealNotchHeight);
  if (state_.state() == NavigationSurfaceState::State::kHidden &&
      !visibility_animation_.is_animating()) {
    top_container_->SetVisible(false);
  }
}

void NavigationSurfaceController::SetFullscreenActive(bool active) {
  state_.SetReasonActive(NavigationSurfaceState::Reason::kFullscreen, active);
}

void NavigationSurfaceController::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &visibility_animation_) {
    ApplyVisibilityFraction(visibility_animation_.GetCurrentValue());
  }
}

void NavigationSurfaceController::AnimationEnded(
    const gfx::Animation* animation) {
  if (animation != &visibility_animation_) {
    return;
  }
  const double value = visibility_animation_.GetCurrentValue();
  ApplyVisibilityFraction(value);
  if (value >= 1.0) {
    state_.FinishReveal();
    StartMouseWatcher();
  } else if (top_container_) {
    top_container_->SetVisible(false);
  }
}

void NavigationSurfaceController::AnimationCanceled(
    const gfx::Animation* animation) {
  AnimationEnded(animation);
}

void NavigationSurfaceController::OnDidChangeFocus(views::View* focused_before,
                                                   views::View* focused_now) {
  UpdateInteractionReasons(focused_now);
}

void NavigationSurfaceController::OnFocusManagerDestroying(
    views::FocusManager* focus_manager) {
  if (focus_manager_ == focus_manager) {
    focus_manager_ = nullptr;
  }
}

void NavigationSurfaceController::MouseMovedOutOfHost() {
  state_.SetReasonActive(NavigationSurfaceState::Reason::kRevealNotchHover,
                         false);
  UpdateInteractionReasons(focus_manager_ ? focus_manager_->GetFocusedView()
                                          : nullptr);
}

void NavigationSurfaceController::OnViewAddedToWidget(
    views::View* observed_view) {
  AttachToWidget();
}

void NavigationSurfaceController::OnViewRemovedFromWidget(
    views::View* observed_view) {
  DetachFromWidget();
}

void NavigationSurfaceController::OnViewIsDeleting(views::View* observed_view) {
  DetachFromWidget();
  host_observation_.Reset();
  host_ = nullptr;
  top_container_ = nullptr;
  toolbar_ = nullptr;
  reveal_notch_ = nullptr;
}

void NavigationSurfaceController::OnRevealNotchEntered() {
  if (!navigation_preferences_.reveal_notch_enabled ||
      !navigation_preferences_.auto_hide_enabled) {
    return;
  }
  StartMouseWatcher();
  state_.SetReasonActive(NavigationSurfaceState::Reason::kRevealNotchHover,
                         true);
}

void NavigationSurfaceController::OnStateChanged(
    NavigationSurfaceState::State state) {
  if (!top_container_ || !reveal_notch_) {
    return;
  }
  if (reduced_motion_) {
    const double target =
        state == NavigationSurfaceState::State::kHidden ? 0.0 : 1.0;
    visibility_animation_.Reset(target);
    ApplyVisibilityFraction(target);
    if (state == NavigationSurfaceState::State::kRevealing) {
      state_.FinishReveal();
    }
    return;
  }
  if (state == NavigationSurfaceState::State::kHidden) {
    visibility_animation_.SetSlideDuration(
        visual_style::kNavigationSurfaceHideDuration);
    visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
    visibility_animation_.Hide();
    return;
  }

  top_container_->SetVisible(true);
  reveal_notch_->SetVisible(true);
  visibility_animation_.SetSlideDuration(
      visual_style::kNavigationSurfaceRevealDuration);
  visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  visibility_animation_.Show();
}

void NavigationSurfaceController::ApplyVisibilityFraction(double value) {
  if (!top_container_ || !reveal_notch_) {
    return;
  }
  const double clamped_value = std::clamp(value, 0.0, 1.0);
  const float opacity = static_cast<float>(clamped_value);
  top_container_->layer()->SetOpacity(opacity);
  top_container_->SetCanProcessEventsWithinSubtree(clamped_value > 0.4);
  gfx::Transform transform;
  // Moving the entire toolbar height made the surface feel detached and caused
  // a visible snap at the end of the animation. A short shared offset keeps it
  // visually anchored to the window while the opacity supplies the transition.
  transform.Translate(
      0, -visual_style::kNavigationSurfaceRevealOffset * (1.0 - clamped_value));
  top_container_->layer()->SetTransform(transform);

  const float notch_opacity =
      static_cast<float>(std::clamp(1.0 - (clamped_value * 2.0), 0.0, 1.0));
  const bool show_notch = navigation_preferences_.auto_hide_enabled &&
                          navigation_preferences_.reveal_notch_enabled &&
                          notch_opacity > 0.0f;
  reveal_notch_->SetVisible(show_notch);
  reveal_notch_->layer()->SetOpacity(notch_opacity);
  reveal_notch_->SetCanProcessEventsWithinSubtree(show_notch &&
                                                  clamped_value < 0.35);
}

void NavigationSurfaceController::AttachToWidget() {
  if (!host_ || !host_->GetWidget() || focus_manager_) {
    return;
  }
  focus_manager_ = host_->GetWidget()->GetFocusManager();
  if (focus_manager_) {
    focus_manager_->AddFocusChangeListener(this);
  }

  // Give users a short, predictable initial look at the controls before the
  // first automatic hide. This lease is independent from later hover/focus.
  state_.SetReasonActive(
      NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus, true);
  initial_presentation_timer_.Start(
      FROM_HERE, base::Seconds(1), this,
      &NavigationSurfaceController::ReleaseInitialPresentationLease);
}

void NavigationSurfaceController::DetachFromWidget() {
  initial_presentation_timer_.Stop();
  if (mouse_watcher_) {
    mouse_watcher_->Stop();
  }
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = nullptr;
  }
}

void NavigationSurfaceController::StartMouseWatcher() {
  if (!host_ || !host_->GetWidget() || !toolbar_) {
    return;
  }
  if (!mouse_watcher_) {
    mouse_watcher_ = std::make_unique<views::MouseWatcher>(
        std::make_unique<views::MouseWatcherViewHost>(toolbar_,
                                                      gfx::Insets::VH(8, 8)),
        this);
  }
  mouse_watcher_->Start(host_->GetWidget()->GetNativeWindow());
}

void NavigationSurfaceController::UpdateInteractionReasons(
    views::View* focused_view) {
  const bool toolbar_focused =
      toolbar_ && focused_view && toolbar_->Contains(focused_view);
  state_.SetReasonActive(
      NavigationSurfaceState::Reason::kKeyboardOrOmniboxFocus, toolbar_focused);

  bool has_child_bubble = false;
  bool has_modal_prompt = false;
  if (host_ && host_->GetWidget()) {
    for (views::Widget* widget : views::Widget::GetAllChildWidgets(
             host_->GetWidget()->GetNativeView())) {
      if (widget == host_->GetWidget() || !widget->IsVisible()) {
        continue;
      }
      has_modal_prompt |= widget->IsModal();
      has_child_bubble |= !widget->IsModal();
    }
  }
  state_.SetReasonActive(NavigationSurfaceState::Reason::kToolbarBubble,
                         has_child_bubble);
  state_.SetReasonActive(NavigationSurfaceState::Reason::kAuthPrompt,
                         has_modal_prompt);
}

void NavigationSurfaceController::ReleaseInitialPresentationLease() {
  UpdateInteractionReasons(focus_manager_ ? focus_manager_->GetFocusedView()
                                          : nullptr);
}

void NavigationSurfaceController::OnAppearancePolicyChanged(
    const appearance::GlassPolicy& policy) {
  if (!toolbar_) {
    return;
  }
  appearance::ApplySurfaceAppearance(
      toolbar_, appearance::AppearanceResolver::Resolve(
                    appearance::SurfaceRole::kFloatingNavigation, policy));
  reduced_motion_ = policy.reduced_motion;
  state_.SetReducedMotion(reduced_motion_);
  if (reduced_motion_) {
    const double target =
        state_.state() == NavigationSurfaceState::State::kHidden ? 0.0 : 1.0;
    visibility_animation_.Reset(target);
    ApplyVisibilityFraction(target);
  }
}

void NavigationSurfaceController::RefreshNavigationPreferences() {
  navigation_preferences_ =
      prefs_ ? appearance::GetFloatingNavigationPreferences(*prefs_)
             : appearance::FloatingNavigationPreferences();
  state_.SetAutoHideDelay(navigation_preferences_.auto_hide_delay);
  state_.SetAutoHideEnabled(navigation_preferences_.auto_hide_enabled);
  ApplyVisibilityFraction(visibility_animation_.GetCurrentValue());
}

}  // namespace ahoi
