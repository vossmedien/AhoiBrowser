// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_swipe_event_handler.h"

#include "base/memory/raw_ptr.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"

namespace ahoi {
namespace {

class WorkspaceSwipeNativeEventMonitorMac final
    : public WorkspaceSwipeNativeEventMonitor,
      public views::NativeWidgetMacEventMonitor::Client {
 public:
  explicit WorkspaceSwipeNativeEventMonitorMac(
      WorkspaceSwipeEventHandler* handler)
      : handler_(handler) {}

  void Start(views::NativeWidgetMacNSWindowHost* host) {
    monitor_ = host->AddEventMonitor(this);
  }

  void NativeWidgetMacEventMonitorOnEvent(ui::Event* event,
                                          bool target_is_this_window,
                                          bool* event_handled) override {
    handler_->OnNativeScrollEvent(event, target_is_this_window, event_handled);
  }

 private:
  const raw_ptr<WorkspaceSwipeEventHandler> handler_;
  std::unique_ptr<views::NativeWidgetMacEventMonitor> monitor_;
};

}  // namespace

std::unique_ptr<WorkspaceSwipeNativeEventMonitor>
CreateWorkspaceSwipeNativeEventMonitor(views::Widget* widget,
                                       WorkspaceSwipeEventHandler* handler) {
  if (!widget || !handler) {
    return nullptr;
  }
  views::NativeWidgetMacNSWindowHost* const host =
      views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
          widget->GetNativeWindow());
  if (!host) {
    return nullptr;
  }
  auto monitor = std::make_unique<WorkspaceSwipeNativeEventMonitorMac>(handler);
  monitor->Start(host);
  return monitor;
}

}  // namespace ahoi
