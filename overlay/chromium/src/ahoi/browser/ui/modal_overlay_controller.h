// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_MODAL_OVERLAY_CONTROLLER_H_
#define AHOI_BROWSER_UI_MODAL_OVERLAY_CONTROLLER_H_

#include <cstdint>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace ahoi {

// Coordinates one browser-window-local modal surface. The panel remains owned
// by its feature controller; this class owns only the full-window scrim,
// animation and focus lifecycle. This lets command bar and sidebar dialogs use
// the same modality without coupling their content or Widget ownership.
class ModalOverlayController final : public views::ViewObserver,
                                     public views::WidgetObserver {
 public:
  ModalOverlayController(views::View* window_host, views::View* center_anchor);
  ModalOverlayController(const ModalOverlayController&) = delete;
  ModalOverlayController& operator=(const ModalOverlayController&) = delete;
  ~ModalOverlayController() override;

  // Anchor for BubbleBorder::FLOAT panels. It is the browser contents area,
  // deliberately excluding the vertical sidebar.
  views::View* center_anchor() const;

  // Shows `panel_widget`, installs the full-window event-blocking scrim and
  // starts the entrance animation. `request_panel_close` must close the panel
  // Widget when called after the exit animation. Returns false if another
  // panel is still active or any required host has already been destroyed.
  bool ShowPanel(views::Widget* panel_widget,
                 base::RepeatingClosure request_panel_close);

  // Requests one controlled close. Repeated requests while closing are
  // consumed without invoking `request_panel_close` more than once.
  bool RequestClose(views::Widget* panel_widget);

  // Called by the panel owner when a Widget closes through another path (for
  // example parent shutdown or a browser command that changes activation).
  void NotifyPanelClosed(views::Widget* panel_widget,
                         bool restore_focus = true);

  // Detaches an owner that is being destroyed. No close callback or focus
  // restoration is performed.
  void DismissPanelImmediately(views::Widget* panel_widget);

  bool IsShowingPanel(const views::Widget* panel_widget) const;

  // Returns whether this browser window currently owns any open or closing
  // Ahoi modal panel. Runtime input gates use this instead of inspecting the
  // always-present scrim view.
  bool IsShowingAnyPanel() const;

  views::View* scrim_view_for_testing() const;

 private:
  enum class State {
    kIdle,
    kOpen,
    kClosing,
  };

  void UpdateScrimBounds();
  void AnimateOpen();
  void FinishClose(uint64_t generation);
  void ScheduleFinishClose(uint64_t generation);
  void ResetActivePanel(bool restore_focus);
  void ScheduleFocusRestore();
  void RestoreFocus();
  void OnScrimPressed();

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // views::WidgetObserver:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetDestroyed(views::Widget* widget) override;

  raw_ptr<views::View> window_host_ = nullptr;
  views::ViewTracker center_anchor_tracker_;
  views::ViewTracker scrim_tracker_;
  views::ViewTracker previously_focused_view_tracker_;
  raw_ptr<views::Widget> panel_widget_ = nullptr;
  base::RepeatingClosure request_panel_close_;
  State state_ = State::kIdle;
  uint64_t close_generation_ = 0;
  int focus_restore_attempts_ = 0;

  base::ScopedObservation<views::View, views::ViewObserver>
      window_host_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      panel_widget_observation_{this};
  base::WeakPtrFactory<ModalOverlayController> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_MODAL_OVERLAY_CONTROLLER_H_
