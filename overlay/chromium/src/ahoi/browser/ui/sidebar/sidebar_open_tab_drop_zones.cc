// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"

namespace ahoi::sidebar {

OpenTabDropPosition OpenTabDropPositionForY(int y, int row_height) {
  const int edge_zone = GetSidebarEdgeDropTargetExtent(row_height);
  if (y < edge_zone) {
    return OpenTabDropPosition::kBefore;
  }
  if (y >= row_height - edge_zone) {
    return OpenTabDropPosition::kAfter;
  }
  return OpenTabDropPosition::kSplit;
}

OpenTabDropPosition NearestOpenTabDropEdge(int y, int row_height) {
  return y < row_height / 2 ? OpenTabDropPosition::kBefore
                            : OpenTabDropPosition::kAfter;
}

bool KeepsOpenTabDropPosition(OpenTabDropPosition current,
                              std::optional<OpenTabDropPosition> next,
                              int y,
                              int row_height) {
  constexpr int kDropZoneHysteresis = 4;
  const int edge_extent = GetSidebarEdgeDropTargetExtent(row_height);
  const int before_boundary = edge_extent;
  const int after_boundary = row_height - edge_extent;
  const int center_boundary = row_height / 2;
  return (current == OpenTabDropPosition::kBefore &&
          next == OpenTabDropPosition::kSplit &&
          y < before_boundary + kDropZoneHysteresis) ||
         (current == OpenTabDropPosition::kSplit &&
          next == OpenTabDropPosition::kBefore &&
          y >= before_boundary - kDropZoneHysteresis) ||
         (current == OpenTabDropPosition::kAfter &&
          next == OpenTabDropPosition::kSplit &&
          y >= after_boundary - kDropZoneHysteresis) ||
         (current == OpenTabDropPosition::kSplit &&
          next == OpenTabDropPosition::kAfter &&
          y < after_boundary + kDropZoneHysteresis) ||
         (current == OpenTabDropPosition::kBefore &&
          next == OpenTabDropPosition::kAfter &&
          y < center_boundary + kDropZoneHysteresis) ||
         (current == OpenTabDropPosition::kAfter &&
          next == OpenTabDropPosition::kBefore &&
          y >= center_boundary - kDropZoneHysteresis);
}

}  // namespace ahoi::sidebar
