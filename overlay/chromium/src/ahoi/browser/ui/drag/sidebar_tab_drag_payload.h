// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DRAG_SIDEBAR_TAB_DRAG_PAYLOAD_H_
#define AHOI_BROWSER_UI_DRAG_SIDEBAR_TAB_DRAG_PAYLOAD_H_

#include <optional>
#include <string>

#include "base/uuid.h"
#include "ui/base/clipboard/clipboard_format_type.h"

namespace ui {
class OSExchangeData;
}

namespace ahoi::drag {

// The stable, View-independent identity carried by native sidebar tab drags.
// Exactly one field is populated. The custom formats and Pickle field order
// are compatibility contracts with already-running Ahoi windows.
struct SidebarTabDragPayload {
  std::optional<base::Uuid> saved_node_id;
  std::optional<int> runtime_tab_handle;

  bool is_saved_tab() const { return saved_node_id.has_value(); }
  bool is_runtime_tab() const { return runtime_tab_handle.has_value(); }
  bool is_valid() const { return is_saved_tab() != is_runtime_tab(); }

  bool operator==(const SidebarTabDragPayload&) const = default;
};

const ui::ClipboardFormatType& SavedSidebarTabDragFormat();
const ui::ClipboardFormatType& RuntimeSidebarTabDragFormat();

void WriteSavedSidebarTabDragPayload(ui::OSExchangeData* data,
                                     const base::Uuid& node_id,
                                     const std::u16string& fallback_title);
void WriteRuntimeSidebarTabDragPayload(ui::OSExchangeData* data,
                                       int runtime_tab_handle,
                                       const std::u16string& fallback_title);

std::optional<base::Uuid> ReadSavedSidebarTabDragPayload(
    const ui::OSExchangeData& data);
std::optional<int> ReadRuntimeSidebarTabDragPayload(
    const ui::OSExchangeData& data);

// Rejects ambiguous data containing both private formats. In particular, the
// portable string fallback is never interpreted as a URL or tab identity.
std::optional<SidebarTabDragPayload> ReadSidebarTabDragPayload(
    const ui::OSExchangeData& data);

}  // namespace ahoi::drag

#endif  // AHOI_BROWSER_UI_DRAG_SIDEBAR_TAB_DRAG_PAYLOAD_H_
