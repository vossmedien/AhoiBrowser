// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::sidebar {

namespace {

using ActiveSidebarDragHosts = std::set<const views::View*>;

ActiveSidebarDragHosts& GetActiveSidebarDragHosts() {
  static base::NoDestructor<ActiveSidebarDragHosts> hosts;
  return *hosts;
}

}  // namespace

void SetBrowserSidebarDragRoutingActive(views::View* sidebar_host,
                                        bool active) {
  if (!sidebar_host) {
    return;
  }
  if (active) {
    GetActiveSidebarDragHosts().insert(sidebar_host);
  } else {
    GetActiveSidebarDragHosts().erase(sidebar_host);
  }
}

bool IsAnyBrowserSidebarDragActive() {
  return !GetActiveSidebarDragHosts().empty();
}

bool BrowserSidebarHostView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats |= ui::OSExchangeData::PICKLED_DATA;
  format_types->insert(drag::SavedSidebarTabDragFormat());
  format_types->insert(drag::RuntimeSidebarTabDragFormat());
  return true;
}

bool BrowserSidebarHostView::AreDropTypesRequired() {
  return true;
}

bool BrowserSidebarHostView::CanDrop(const ui::OSExchangeData& data) {
  // The host is a routing shield, not a semantic drop target. Explicit tree,
  // runtime-row and New Group descendants win first. If the pointer is over a
  // sidebar gap or header, accepting the format here prevents DropHelper from
  // walking up to BrowserView and treating the content hidden under a floating
  // card as a split target.
  return drag::ReadSidebarTabDragPayload(data).has_value();
}

int BrowserSidebarHostView::OnDragUpdated(
    const ui::DropTargetEvent& /*event*/) {
  return ui::DragDropTypes::DRAG_NONE;
}

views::View::DropCallback BrowserSidebarHostView::GetDropCallback(
    const ui::DropTargetEvent& /*event*/) {
  // Keep this callable even if a platform reports a stale non-NONE operation
  // after OnDragUpdated() rejected the gap. DropHelper invokes a selected
  // target's callback unconditionally on that path.
  return base::DoNothing();
}

}  // namespace ahoi::sidebar
