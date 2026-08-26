// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_EVENT_HANDLER_H_
#define AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_EVENT_HANDLER_H_

#include <memory>

#include "ahoi/browser/navigation/cmd_scroll_tab_switcher.h"
#include "ahoi/browser/navigation/workspace_swipe_tracker.h"
#include "base/functional/callback.h"
#include "build/build_config.h"
#include "ui/events/event_handler.h"

namespace ui {
class Event;
class ScrollEvent;
}  // namespace ui

#if BUILDFLAG(IS_MAC)
namespace views {
class Widget;
}
#endif

namespace ahoi {

#if BUILDFLAG(IS_MAC)
// Platform-neutral ownership boundary for the private Objective-C++ local
// event-monitor adapter.
class WorkspaceSwipeNativeEventMonitor {
 public:
  virtual ~WorkspaceSwipeNativeEventMonitor() = default;
};
#endif

// Root event adapter for macOS workspace swipes. This deliberately is an
// EventHandler rather than a View: registering BrowserView itself as a
// pre-target handler also invokes View's mouse-drag machinery before the real
// tab-row target, which steals native sidebar drag-and-drop.
class WorkspaceSwipeEventHandler final : public ui::EventHandler {
 public:
  using SwitchWorkspaceCallback = base::RepeatingCallback<bool(int delta)>;
  using SwitchTabCallback = base::RepeatingCallback<bool(int delta)>;
  using PreviewTabCallback = base::RepeatingCallback<bool(int delta)>;
  using CanStartWorkspaceSwipeCallback = base::RepeatingCallback<bool()>;

  explicit WorkspaceSwipeEventHandler(
      SwitchWorkspaceCallback switch_workspace_callback,
      SwitchTabCallback switch_tab_callback = {},
      CanStartWorkspaceSwipeCallback can_start_workspace_swipe_callback = {},
      PreviewTabCallback preview_tab_callback = {},
      WorkspaceSwipeSettings workspace_settings = {},
      CmdScrollTabSettings cmd_scroll_settings = {});
  WorkspaceSwipeEventHandler(const WorkspaceSwipeEventHandler&) = delete;
  WorkspaceSwipeEventHandler& operator=(const WorkspaceSwipeEventHandler&) =
      delete;
  ~WorkspaceSwipeEventHandler() override;

  void Cancel();
  bool SetWorkspaceSettings(WorkspaceSwipeSettings settings);
  bool SetCmdScrollSettings(CmdScrollTabSettings settings);

#if BUILDFLAG(IS_MAC)
  // The macOS native event monitor observes scroll-wheel events before AppKit
  // dispatches them to a Views/WebContents target. Keeping the monitor owned
  // here makes its lifetime identical to the BrowserView-owned handler.
  void SetNativeEventMonitor(
      std::unique_ptr<WorkspaceSwipeNativeEventMonitor> monitor);

  // Called only by the private Objective-C++ event-monitor adapter.
  void OnNativeScrollEvent(ui::Event* event,
                           bool target_is_this_window,
                           bool* event_handled);
#endif

  // ui::EventHandler:
  void OnScrollEvent(ui::ScrollEvent* event) override;

 private:
  void ProcessScrollEvent(ui::ScrollEvent* event,
                          bool require_pretarget_phase,
                          bool* event_handled);

  bool ProcessCmdScrollEvent(ui::ScrollEvent* event, bool* event_handled);

  SwitchWorkspaceCallback switch_workspace_callback_;
  SwitchTabCallback switch_tab_callback_;
  CanStartWorkspaceSwipeCallback can_start_workspace_swipe_callback_;
  PreviewTabCallback preview_tab_callback_;
  WorkspaceSwipeTracker tracker_;
  CmdScrollTabSwitcher cmd_scroll_tab_switcher_;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<WorkspaceSwipeNativeEventMonitor> native_event_monitor_;
#endif
};

#if BUILDFLAG(IS_MAC)
// Creates the monitor through the macOS NativeWidgetNSWindowHost bridge. The
// Objective-C++ implementation is isolated from C++ BrowserView files.
std::unique_ptr<WorkspaceSwipeNativeEventMonitor>
CreateWorkspaceSwipeNativeEventMonitor(views::Widget* widget,
                                       WorkspaceSwipeEventHandler* handler);
#endif

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_WORKSPACE_SWIPE_EVENT_HANDLER_H_
