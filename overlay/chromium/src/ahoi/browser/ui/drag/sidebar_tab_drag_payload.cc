// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"

#include "base/check.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
#include "ui/base/dragdrop/os_exchange_data.h"

namespace ahoi::drag {

namespace {

constexpr char kSavedSidebarTabClipboardFormat[] =
    "chromium/x-ahoi-sidebar-tree-node";
constexpr char kRuntimeSidebarTabClipboardFormat[] =
    "chromium/x-ahoi-runtime-tab";

}  // namespace

const ui::ClipboardFormatType& SavedSidebarTabDragFormat() {
  static base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::CustomPlatformType(
          kSavedSidebarTabClipboardFormat));
  return *format;
}

const ui::ClipboardFormatType& RuntimeSidebarTabDragFormat() {
  static base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::CustomPlatformType(
          kRuntimeSidebarTabClipboardFormat));
  return *format;
}

void WriteSavedSidebarTabDragPayload(ui::OSExchangeData* data,
                                     const base::Uuid& node_id,
                                     const std::u16string& fallback_title) {
  CHECK(data);
  CHECK(node_id.is_valid());
  data->SetString(fallback_title);
  base::Pickle pickle;
  pickle.WriteString(node_id.AsLowercaseString());
  data->SetPickledData(SavedSidebarTabDragFormat(), pickle);
}

void WriteRuntimeSidebarTabDragPayload(ui::OSExchangeData* data,
                                       int runtime_tab_handle,
                                       const std::u16string& fallback_title) {
  CHECK(data);
  CHECK_GE(runtime_tab_handle, 0);
  data->SetString(fallback_title);
  base::Pickle pickle;
  pickle.WriteInt(runtime_tab_handle);
  data->SetPickledData(RuntimeSidebarTabDragFormat(), pickle);
}

std::optional<base::Uuid> ReadSavedSidebarTabDragPayload(
    const ui::OSExchangeData& data) {
  if (!data.HasCustomFormat(SavedSidebarTabDragFormat())) {
    return std::nullopt;
  }
  std::optional<base::Pickle> pickle =
      data.GetPickledData(SavedSidebarTabDragFormat());
  if (!pickle.has_value()) {
    return std::nullopt;
  }
  base::PickleIterator iterator(*pickle);
  std::string serialized;
  if (!iterator.ReadString(&serialized)) {
    return std::nullopt;
  }
  base::Uuid node_id = base::Uuid::ParseLowercase(serialized);
  return node_id.is_valid() ? std::make_optional(node_id) : std::nullopt;
}

std::optional<int> ReadRuntimeSidebarTabDragPayload(
    const ui::OSExchangeData& data) {
  if (!data.HasCustomFormat(RuntimeSidebarTabDragFormat())) {
    return std::nullopt;
  }
  std::optional<base::Pickle> pickle =
      data.GetPickledData(RuntimeSidebarTabDragFormat());
  if (!pickle.has_value()) {
    return std::nullopt;
  }
  base::PickleIterator iterator(*pickle);
  int runtime_tab_handle = -1;
  if (!iterator.ReadInt(&runtime_tab_handle) || runtime_tab_handle < 0) {
    return std::nullopt;
  }
  return runtime_tab_handle;
}

std::optional<SidebarTabDragPayload> ReadSidebarTabDragPayload(
    const ui::OSExchangeData& data) {
  const bool has_saved_format =
      data.HasCustomFormat(SavedSidebarTabDragFormat());
  const bool has_runtime_format =
      data.HasCustomFormat(RuntimeSidebarTabDragFormat());
  if (has_saved_format == has_runtime_format) {
    return std::nullopt;
  }
  const std::optional<base::Uuid> saved_node_id =
      ReadSavedSidebarTabDragPayload(data);
  const std::optional<int> runtime_tab_handle =
      ReadRuntimeSidebarTabDragPayload(data);
  if (saved_node_id.has_value() == runtime_tab_handle.has_value()) {
    return std::nullopt;
  }
  return SidebarTabDragPayload{.saved_node_id = saved_node_id,
                               .runtime_tab_handle = runtime_tab_handle};
}

}  // namespace ahoi::drag
