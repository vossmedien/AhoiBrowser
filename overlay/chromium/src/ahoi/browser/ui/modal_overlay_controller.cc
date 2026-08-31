// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/modal_overlay_controller.h"

#include <memory>
#include <utility>

#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi {

namespace {

class ModalScrimView final : public views::View {
  METADATA_HEADER(ModalScrimView, views::View)

 public:
  explicit ModalScrimView(base::RepeatingClosure pressed_callback)
      : pressed_callback_(std::move(pressed_callback)) {
    SetCanProcessEventsWithinSubtree(true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    SetBackground(views::CreateSolidBackground(visual_style::kModalScrim));
    GetViewAccessibility().SetIsIgnored(true);
    GetViewAccessibility().SetIsInvisible(true);
    SetVisible(false);
  }

  ModalScrimView(const ModalScrimView&) = delete;
  ModalScrimView& operator=(const ModalScrimView&) = delete;
  ~ModalScrimView() override = default;

  bool OnMousePressed(const ui::MouseEvent& event) override {
    pressed_callback_.Run();
    return true;
  }

  bool OnMouseWheel(const ui::MouseWheelEvent& event) override { return true; }

  void OnScrollEvent(ui::ScrollEvent* event) override { event->SetHandled(); }

  void OnGestureEvent(ui::GestureEvent* event) override {
    if (event->type() == ui::EventType::kGestureTapDown) {
      pressed_callback_.Run();
    }
    event->SetHandled();
  }

 private:
  base::RepeatingClosure pressed_callback_;
};

BEGIN_METADATA(ModalScrimView)
END_METADATA

}  // namespace

ModalOverlayController::ModalOverlayController(views::View* window_host,
                                               views::View* center_anchor)
    : window_host_(window_host), center_anchor_tracker_(center_anchor) {
  CHECK(window_host_);
  CHECK(center_anchor);
  window_host_observation_.Observe(window_host_);

  auto scrim = std::make_unique<ModalScrimView>(base::BindRepeating(
      &ModalOverlayController::OnScrimPressed, weak_ptr_factory_.GetWeakPtr()));
  views::View* const scrim_ptr = window_host_->AddChildView(std::move(scrim));
  scrim_tracker_.SetView(scrim_ptr);
  UpdateScrimBounds();
}

ModalOverlayController::~ModalOverlayController() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  ResetActivePanel(/*restore_focus=*/false);
  window_host_observation_.Reset();
  center_anchor_tracker_.SetView(nullptr);
  previously_focused_view_tracker_.SetView(nullptr);

  views::View* const scrim = scrim_tracker_.view();
  if (window_host_ && scrim && scrim->parent() == window_host_) {
    window_host_->RemoveChildViewT(scrim);
  }
  scrim_tracker_.SetView(nullptr);
  window_host_ = nullptr;
}

views::View* ModalOverlayController::center_anchor() const {
  return const_cast<views::View*>(center_anchor_tracker_.view());
}

bool ModalOverlayController::ShowPanel(
    views::Widget* panel_widget,
    base::RepeatingClosure request_panel_close) {
  views::View* const scrim = scrim_tracker_.view();
  views::View* const anchor = center_anchor();
  if (!window_host_ || !scrim || !anchor || !window_host_->GetWidget() ||
      !panel_widget || request_panel_close.is_null() || panel_widget_) {
    return false;
  }

  if (!previously_focused_view_tracker_.view()) {
    if (views::FocusManager* const focus_manager =
            window_host_->GetFocusManager()) {
      previously_focused_view_tracker_.SetView(focus_manager->GetFocusedView());
    }
  }

  panel_widget_ = panel_widget;
  request_panel_close_ = std::move(request_panel_close);
  state_ = State::kOpen;
  panel_widget_observation_.Observe(panel_widget_);

  UpdateScrimBounds();
  window_host_->ReorderChildView(scrim, window_host_->children().size() - 1);
  scrim->layer()->GetAnimator()->AbortAllAnimations();
  scrim->layer()->SetOpacity(0.0f);
  scrim->SetVisible(true);

  if (ui::Layer* const panel_layer = panel_widget_->GetLayer()) {
    panel_layer->GetAnimator()->AbortAllAnimations();
    panel_layer->SetOpacity(0.0f);
  }
  if (views::View* const root_view = panel_widget_->GetRootView()) {
    root_view->SetCanProcessEventsWithinSubtree(true);
  }

  panel_widget_->Show();
  if (panel_widget_ != panel_widget || state_ == State::kIdle) {
    return false;
  }
  AnimateOpen();
  return true;
}

bool ModalOverlayController::RequestClose(views::Widget* panel_widget) {
  if (!panel_widget || panel_widget != panel_widget_) {
    return false;
  }
  if (state_ == State::kClosing) {
    return true;
  }
  if (state_ != State::kOpen) {
    return false;
  }

  state_ = State::kClosing;
  const uint64_t generation = ++close_generation_;
  if (views::View* const root_view = panel_widget_->GetRootView()) {
    root_view->SetCanProcessEventsWithinSubtree(false);
  }

  views::View* const scrim = scrim_tracker_.view();
  ui::Layer* const scrim_layer = scrim ? scrim->layer() : nullptr;
  ui::Layer* const panel_layer = panel_widget_->GetLayer();
  if (!scrim_layer && !panel_layer) {
    FinishClose(generation);
    return true;
  }

  views::AnimationBuilder builder;
  auto& sequence =
      builder
          .SetPreemptionStrategy(
              ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
          .OnEnded(base::BindOnce(&ModalOverlayController::FinishClose,
                                  weak_ptr_factory_.GetWeakPtr(), generation))
          .OnAborted(
              base::BindOnce(&ModalOverlayController::ScheduleFinishClose,
                             weak_ptr_factory_.GetWeakPtr(), generation))
          .Once()
          .SetDuration(gfx::Animation::RichAnimationDuration(
              visual_style::kModalFadeOutDuration));
  if (scrim_layer) {
    sequence.SetOpacity(scrim_layer, 0.0f, gfx::Tween::EASE_IN);
  }
  if (panel_layer) {
    sequence.SetOpacity(panel_layer, 0.0f, gfx::Tween::EASE_IN);
  }
  return true;
}

void ModalOverlayController::NotifyPanelClosed(views::Widget* panel_widget,
                                               bool restore_focus) {
  if (panel_widget && panel_widget == panel_widget_) {
    ResetActivePanel(restore_focus);
    if (restore_focus) {
      ScheduleFocusRestore();
    }
  }
}

void ModalOverlayController::DismissPanelImmediately(
    views::Widget* panel_widget) {
  if (panel_widget && panel_widget == panel_widget_) {
    ResetActivePanel(/*restore_focus=*/false);
  }
}

bool ModalOverlayController::IsShowingPanel(
    const views::Widget* panel_widget) const {
  return panel_widget && panel_widget == panel_widget_ &&
         state_ != State::kIdle;
}

bool ModalOverlayController::IsShowingAnyPanel() const {
  return panel_widget_ && state_ != State::kIdle;
}

views::View* ModalOverlayController::scrim_view_for_testing() const {
  return const_cast<views::View*>(scrim_tracker_.view());
}

void ModalOverlayController::UpdateScrimBounds() {
  if (window_host_ && scrim_tracker_.view()) {
    scrim_tracker_.view()->SetBoundsRect(window_host_->GetLocalBounds());
  }
}

void ModalOverlayController::AnimateOpen() {
  views::View* const scrim = scrim_tracker_.view();
  ui::Layer* const scrim_layer = scrim ? scrim->layer() : nullptr;
  ui::Layer* const panel_layer =
      panel_widget_ ? panel_widget_->GetLayer() : nullptr;
  if (!scrim_layer && !panel_layer) {
    return;
  }

  views::AnimationBuilder builder;
  auto& sequence = builder
                       .SetPreemptionStrategy(
                           ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
                       .Once()
                       .SetDuration(gfx::Animation::RichAnimationDuration(
                           visual_style::kModalFadeInDuration));
  if (scrim_layer) {
    sequence.SetOpacity(scrim_layer, 1.0f, gfx::Tween::EASE_OUT);
  }
  if (panel_layer) {
    sequence.SetOpacity(panel_layer, 1.0f, gfx::Tween::EASE_OUT);
  }
}

void ModalOverlayController::FinishClose(uint64_t generation) {
  if (state_ != State::kClosing || generation != close_generation_) {
    return;
  }
  base::RepeatingClosure request_panel_close = request_panel_close_;
  ResetActivePanel(/*restore_focus=*/true);
  if (request_panel_close) {
    request_panel_close.Run();
  }
  ScheduleFocusRestore();
}

void ModalOverlayController::ScheduleFinishClose(uint64_t generation) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ModalOverlayController::FinishClose,
                                weak_ptr_factory_.GetWeakPtr(), generation));
}

void ModalOverlayController::ResetActivePanel(bool restore_focus) {
  if (!panel_widget_) {
    if (!restore_focus) {
      previously_focused_view_tracker_.SetView(nullptr);
    }
    return;
  }

  views::Widget* const panel_widget = panel_widget_;
  state_ = State::kIdle;
  ++close_generation_;
  panel_widget_observation_.Reset();
  panel_widget_ = nullptr;
  request_panel_close_.Reset();

  if (ui::Layer* const panel_layer = panel_widget->GetLayer()) {
    panel_layer->GetAnimator()->AbortAllAnimations();
  }
  if (views::View* const scrim = scrim_tracker_.view()) {
    scrim->layer()->GetAnimator()->AbortAllAnimations();
    scrim->layer()->SetOpacity(0.0f);
    scrim->SetVisible(false);
  }

  if (!restore_focus) {
    previously_focused_view_tracker_.SetView(nullptr);
  }
}

void ModalOverlayController::ScheduleFocusRestore() {
  if (!previously_focused_view_tracker_.view()) {
    return;
  }
  focus_restore_attempts_ = 0;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ModalOverlayController::RestoreFocus,
                                weak_ptr_factory_.GetWeakPtr()));
}

void ModalOverlayController::RestoreFocus() {
  if (state_ != State::kIdle || panel_widget_) {
    return;
  }
  views::View* const previously_focused =
      previously_focused_view_tracker_.view();
  if (!previously_focused || !previously_focused->GetVisible() ||
      !previously_focused->GetWidget()) {
    previously_focused_view_tracker_.SetView(nullptr);
    return;
  }
  previously_focused_view_tracker_.SetView(nullptr);
  // RequestFocus only updates this Widget's FocusManager; it does not activate
  // or raise an inactive browser window. Keeping the intended focused child
  // therefore remains safe when the user has switched windows, and avoids a
  // race where the closing child Widget briefly reports its parent inactive
  // and the focus target was previously discarded after two immediate tasks.
  previously_focused->RequestFocus();
}

void ModalOverlayController::OnScrimPressed() {
  if (panel_widget_) {
    RequestClose(panel_widget_);
  }
}

void ModalOverlayController::OnViewBoundsChanged(views::View* observed_view) {
  if (observed_view == window_host_) {
    UpdateScrimBounds();
  }
}

void ModalOverlayController::OnViewIsDeleting(views::View* observed_view) {
  if (observed_view != window_host_) {
    return;
  }
  weak_ptr_factory_.InvalidateWeakPtrs();
  ResetActivePanel(/*restore_focus=*/false);
  window_host_observation_.Reset();
  window_host_ = nullptr;
}

void ModalOverlayController::OnWidgetVisibilityChanged(views::Widget* widget,
                                                       bool visible) {
  if (!visible && widget == panel_widget_) {
    ResetActivePanel(/*restore_focus=*/true);
    ScheduleFocusRestore();
  }
}

void ModalOverlayController::OnWidgetDestroyed(views::Widget* widget) {
  if (widget == panel_widget_) {
    ResetActivePanel(/*restore_focus=*/true);
    ScheduleFocusRestore();
  }
}

}  // namespace ahoi
