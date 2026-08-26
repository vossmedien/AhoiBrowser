// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_swipe_event_handler.h"

#include <utility>

#include "base/check.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"

namespace ahoi {

WorkspaceSwipeEventHandler::WorkspaceSwipeEventHandler(
    SwitchWorkspaceCallback switch_workspace_callback,
    SwitchTabCallback switch_tab_callback,
    CanStartWorkspaceSwipeCallback can_start_workspace_swipe_callback,
    PreviewTabCallback preview_tab_callback,
    WorkspaceSwipeSettings workspace_settings,
    CmdScrollTabSettings cmd_scroll_settings)
    : switch_workspace_callback_(std::move(switch_workspace_callback)),
      switch_tab_callback_(std::move(switch_tab_callback)),
      can_start_workspace_swipe_callback_(
          std::move(can_start_workspace_swipe_callback)),
      preview_tab_callback_(std::move(preview_tab_callback)),
      tracker_(std::move(workspace_settings)),
      cmd_scroll_tab_switcher_(std::move(cmd_scroll_settings)) {
  CHECK(switch_workspace_callback_);
}

WorkspaceSwipeEventHandler::~WorkspaceSwipeEventHandler() = default;

void WorkspaceSwipeEventHandler::Cancel() {
  tracker_.OnGestureCancel();
  cmd_scroll_tab_switcher_.Cancel();
}

bool WorkspaceSwipeEventHandler::SetWorkspaceSettings(
    WorkspaceSwipeSettings settings) {
  return tracker_.SetSettings(std::move(settings));
}

bool WorkspaceSwipeEventHandler::SetCmdScrollSettings(
    CmdScrollTabSettings settings) {
  return cmd_scroll_tab_switcher_.SetSettings(std::move(settings));
}

#if BUILDFLAG(IS_MAC)
void WorkspaceSwipeEventHandler::SetNativeEventMonitor(
    std::unique_ptr<WorkspaceSwipeNativeEventMonitor> monitor) {
  native_event_monitor_ = std::move(monitor);
}
#endif

void WorkspaceSwipeEventHandler::OnScrollEvent(ui::ScrollEvent* event) {
  ProcessScrollEvent(event, /*require_pretarget_phase=*/true,
                     /*event_handled=*/nullptr);
}

#if BUILDFLAG(IS_MAC)
void WorkspaceSwipeEventHandler::OnNativeScrollEvent(ui::Event* event,
                                                     bool target_is_this_window,
                                                     bool* event_handled) {
  if (!target_is_this_window || !event_handled || *event_handled || !event ||
      !event->IsScrollEvent()) {
    return;
  }
  ProcessScrollEvent(event->AsScrollEvent(),
                     /*require_pretarget_phase=*/false, event_handled);
}
#endif

void WorkspaceSwipeEventHandler::ProcessScrollEvent(
    ui::ScrollEvent* event,
    bool require_pretarget_phase,
    bool* event_handled) {
  // The native monitor sees both native chrome and rendered content. The
  // fallback Views adapter still requires pre-target delivery when used by
  // tests or another embedder.
  if (!event ||
      (require_pretarget_phase && event->phase() != ui::EP_PRETARGET)) {
    return;
  }

  // Cmd+scroll is intentionally handled before workspace swipe recognition.
  // This keeps the modifier gesture deterministic and does not alter
  // Chromium's normal two-finger back/forward event path.
  if (switch_tab_callback_ && event->IsCommandDown()) {
    const bool starts_cmd_scroll =
        event->scroll_event_phase() == ui::ScrollEventPhase::kBegan ||
        event->momentum_phase() == ui::EventMomentumPhase::MAY_BEGIN;
    if (starts_cmd_scroll) {
      // A modifier gesture must never leave the workspace tracker half-open;
      // otherwise the next unmodified event could commit the old swipe.
      tracker_.OnGestureCancel();
    }
    if (ProcessCmdScrollEvent(event, event_handled) || starts_cmd_scroll) {
      return;
    }
  }

  const ui::ScrollEventPhase direct_phase = event->scroll_event_phase();
  const ui::EventMomentumPhase momentum_phase = event->momentum_phase();
  // On macOS, ScrollEvent's explicit direct phase is currently not populated
  // (ui/events/event.cc). AppKit's direct NSEvent phase is instead adapted to
  // MAY_BEGIN / END in the momentum field while changed events arrive with
  // both phase fields set to none. Treat that sequence as a first-class direct
  // two-finger gesture; otherwise the production Mac path can never start the
  // tracker even though synthetic explicit-phase unit tests pass.
  const bool legacy_macos_begin =
      direct_phase == ui::ScrollEventPhase::kNone &&
      momentum_phase == ui::EventMomentumPhase::MAY_BEGIN;
  if (direct_phase == ui::ScrollEventPhase::kNone &&
      momentum_phase == ui::EventMomentumPhase::NONE && !tracker_.active()) {
    return;
  }

  if (direct_phase == ui::ScrollEventPhase::kBegan || legacy_macos_begin) {
    // Chromium already owns horizontal page-history gestures in WebContents.
    // Ahoi starts a workspace gesture only in its explicit chrome region, so
    // the page never begins one interaction and finishes another.
    if (can_start_workspace_swipe_callback_ &&
        !can_start_workspace_swipe_callback_.Run()) {
      tracker_.OnGestureCancel();
      return;
    }
    tracker_.OnGestureBegin();
  }

  WorkspaceSwipeDecision decision = WorkspaceSwipeDecision::kNone;
  const bool is_momentum_update =
      momentum_phase == ui::EventMomentumPhase::BEGAN ||
      momentum_phase == ui::EventMomentumPhase::INERTIAL_UPDATE;
  if (is_momentum_update) {
    decision = tracker_.OnMomentum();
  } else if (direct_phase == ui::ScrollEventPhase::kBegan ||
             direct_phase == ui::ScrollEventPhase::kUpdate ||
             direct_phase == ui::ScrollEventPhase::kEnd || legacy_macos_begin ||
             (direct_phase == ui::ScrollEventPhase::kNone &&
              momentum_phase == ui::EventMomentumPhase::NONE &&
              tracker_.active()) ||
             (momentum_phase == ui::EventMomentumPhase::END &&
              tracker_.active() && !tracker_.saw_momentum())) {
    decision = tracker_.OnGestureUpdate(event->x_offset_ordinal(),
                                        event->y_offset_ordinal());
  }

  if (direct_phase == ui::ScrollEventPhase::kEnd) {
    const WorkspaceSwipeDecision end_decision = tracker_.OnGestureEnd();
    if (decision == WorkspaceSwipeDecision::kNone) {
      decision = end_decision;
    }
  }
  if (momentum_phase == ui::EventMomentumPhase::END ||
      momentum_phase == ui::EventMomentumPhase::BLOCKED) {
    const WorkspaceSwipeDecision momentum_decision =
        tracker_.saw_momentum() ? tracker_.OnMomentum()
                                : WorkspaceSwipeDecision::kNone;
    const WorkspaceSwipeDecision end_decision = tracker_.OnGestureEnd();
    if (decision == WorkspaceSwipeDecision::kNone) {
      decision = momentum_decision != WorkspaceSwipeDecision::kNone
                     ? momentum_decision
                     : end_decision;
    }
  }

  bool consume = decision == WorkspaceSwipeDecision::kConsume;
  if (decision == WorkspaceSwipeDecision::kSwitchPrevious ||
      decision == WorkspaceSwipeDecision::kSwitchNext) {
    const int delta = decision == WorkspaceSwipeDecision::kSwitchNext ? 1 : -1;
    consume = switch_workspace_callback_.Run(delta);
    if (!consume) {
      tracker_.OnGestureCancel();
    }
  }
  if (consume) {
    if (event_handled) {
      // NativeWidgetMacEventMonitor is outside the Views event dispatcher;
      // setting this flag causes AppKit to swallow the original NSEvent.
      *event_handled = true;
    } else {
      event->SetHandled();
      event->StopPropagation();
    }
  }
}

bool WorkspaceSwipeEventHandler::ProcessCmdScrollEvent(ui::ScrollEvent* event,
                                                       bool* event_handled) {
  const CmdScrollTabDecision decision = cmd_scroll_tab_switcher_.OnScroll(
      event->x_offset_ordinal(), event->y_offset_ordinal(),
      event->scroll_event_phase(), event->momentum_phase(),
      event->time_stamp());
  if (decision == CmdScrollTabDecision::kNone) {
    return false;
  }

  bool consume = decision == CmdScrollTabDecision::kConsume;
  if (decision == CmdScrollTabDecision::kPreviewPrevious ||
      decision == CmdScrollTabDecision::kPreviewNext) {
    const int delta = decision == CmdScrollTabDecision::kPreviewNext ? 1 : -1;
    consume = preview_tab_callback_ && preview_tab_callback_.Run(delta);
  }
  if (decision == CmdScrollTabDecision::kSwitchPrevious ||
      decision == CmdScrollTabDecision::kSwitchNext) {
    const int delta = decision == CmdScrollTabDecision::kSwitchNext ? 1 : -1;
    consume = switch_tab_callback_.Run(delta);
    if (!consume) {
      cmd_scroll_tab_switcher_.Cancel();
    }
  }
  if (!consume) {
    return false;
  }
  if (event_handled) {
    *event_handled = true;
  } else {
    event->SetHandled();
    event->StopPropagation();
  }
  return true;
}

}  // namespace ahoi
