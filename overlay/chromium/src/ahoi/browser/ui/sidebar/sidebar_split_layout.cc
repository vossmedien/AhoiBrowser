// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"

#include <algorithm>

#include "ahoi/browser/ui/visual_style.h"

namespace ahoi::sidebar {

namespace {

constexpr int kGap = visual_style::kSidebarSplitPaneGap;

int SplitAt(int extent, double ratio, int gap) {
  const int available_extent = std::max(0, extent - gap);
  return std::clamp(static_cast<int>(available_extent * ratio), 0,
                    available_extent);
}

}  // namespace

int GetSplitRowPreferredHeight(
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data,
    int standard_row_height) {
  if (standard_row_height <= 0) {
    return 0;
  }

  size_t visual_rows = 1;
  if (segment_count >= 4) {
    visual_rows = 2;
  } else if (segment_count == 3) {
    if (visual_data.arrangement() == split_tabs::SplitTabArrangement::kLinear) {
      visual_rows =
          visual_data.split_layout() == split_tabs::SplitTabLayout::kStacked
              ? 3
              : 1;
    } else {
      visual_rows = 2;
    }
  } else if (segment_count == 2 && visual_data.split_layout() ==
                                       split_tabs::SplitTabLayout::kStacked) {
    visual_rows = 2;
  }

  const int adaptive_height =
      static_cast<int>(visual_rows) *
          visual_style::kSidebarSplitPaneMinimumHeight +
      static_cast<int>(visual_rows - 1) * visual_style::kSidebarSplitPaneGap;
  return std::max(standard_row_height, adaptive_height);
}

std::vector<SidebarSplitSeparator> GetSidebarSplitSeparators(
    const std::vector<gfx::Rect>& segment_bounds,
    const gfx::RectF& paint_bounds) {
  std::vector<SidebarSplitSeparator> separators;
  if (paint_bounds.IsEmpty()) {
    return separators;
  }

  for (size_t first = 0; first < segment_bounds.size(); ++first) {
    for (size_t second = first + 1; second < segment_bounds.size(); ++second) {
      const gfx::Rect& a = segment_bounds[first];
      const gfx::Rect& b = segment_bounds[second];
      const float overlap_top = std::max(
          static_cast<float>(std::max(a.y(), b.y())), paint_bounds.y());
      const float overlap_bottom =
          std::min(static_cast<float>(std::min(a.bottom(), b.bottom())),
                   paint_bounds.bottom());
      const float overlap_left = std::max(
          static_cast<float>(std::max(a.x(), b.x())), paint_bounds.x());
      const float overlap_right =
          std::min(static_cast<float>(std::min(a.right(), b.right())),
                   paint_bounds.right());

      if (overlap_bottom > overlap_top) {
        const int gap =
            a.right() <= b.x() ? b.x() - a.right() : a.x() - b.right();
        if (gap >= 0 && gap <= kGap) {
          const float divider_x =
              static_cast<float>(a.right() <= b.x() ? a.right() + gap / 2.0f
                                                    : b.right() + gap / 2.0f);
          if (divider_x >= paint_bounds.x() &&
              divider_x <= paint_bounds.right()) {
            separators.push_back(
                {.start = gfx::PointF(divider_x, overlap_top),
                 .end = gfx::PointF(divider_x, overlap_bottom)});
          }
        }
      }
      if (overlap_right > overlap_left) {
        const int gap =
            a.bottom() <= b.y() ? b.y() - a.bottom() : a.y() - b.bottom();
        if (gap >= 0 && gap <= kGap) {
          const float divider_y =
              static_cast<float>(a.bottom() <= b.y() ? a.bottom() + gap / 2.0f
                                                     : b.bottom() + gap / 2.0f);
          if (divider_y >= paint_bounds.y() &&
              divider_y <= paint_bounds.bottom()) {
            separators.push_back(
                {.start = gfx::PointF(overlap_left, divider_y),
                 .end = gfx::PointF(overlap_right, divider_y)});
          }
        }
      }
    }
  }
  return separators;
}

int GetSidebarEdgeDropTargetExtent(int row_height) {
  if (row_height <= 0) {
    return 0;
  }
  return std::clamp(row_height * 3 / 10, 1, std::max(1, (row_height - 1) / 2));
}

gfx::RectF GetSidebarEdgeDropTargetBounds(const gfx::Rect& row_bounds,
                                          bool trailing_edge) {
  if (row_bounds.IsEmpty()) {
    return gfx::RectF();
  }
  const int zone_height = GetSidebarEdgeDropTargetExtent(row_bounds.height());
  const int horizontal_inset =
      std::min(visual_style::kSidebarTabRowHorizontalInset,
               std::max(0, (row_bounds.width() - 1) / 2));
  return gfx::RectF(
      static_cast<float>(row_bounds.x() + horizontal_inset),
      static_cast<float>(trailing_edge ? row_bounds.bottom() - zone_height
                                       : row_bounds.y()),
      static_cast<float>(
          std::max(0, row_bounds.width() - 2 * horizontal_inset)),
      static_cast<float>(zone_height));
}

gfx::Rect GetSplitSegmentBounds(
    const gfx::Rect& bounds,
    size_t segment,
    size_t segment_count,
    const split_tabs::SplitTabVisualData& visual_data) {
  if (segment_count == 2) {
    if (visual_data.split_layout() == split_tabs::SplitTabLayout::kSideBySide) {
      const int first_width = SplitAt(bounds.width(), visual_data.split_ratio(),
                                      visual_style::kSidebarSplitPaneGap);
      return segment == 0
                 ? gfx::Rect(bounds.x(), bounds.y(), first_width,
                             bounds.height())
                 : gfx::Rect(
                       bounds.x() + first_width +
                           visual_style::kSidebarSplitPaneGap,
                       bounds.y(),
                       std::max(0, bounds.width() - first_width -
                                       visual_style::kSidebarSplitPaneGap),
                       bounds.height());
    }
    const int first_height = SplitAt(bounds.height(), visual_data.split_ratio(),
                                     visual_style::kSidebarSplitPaneGap);
    return segment == 0
               ? gfx::Rect(bounds.x(), bounds.y(), bounds.width(), first_height)
               : gfx::Rect(bounds.x(),
                           bounds.y() + first_height +
                               visual_style::kSidebarSplitPaneGap,
                           bounds.width(),
                           std::max(0, bounds.height() - first_height -
                                           visual_style::kSidebarSplitPaneGap));
  }

  if (segment_count > 3) {
    constexpr size_t kColumns = 2;
    const size_t rows = (segment_count + kColumns - 1) / kColumns;
    const int content_width =
        std::max(0, bounds.width() - visual_style::kSidebarSplitPaneGap);
    const int content_height =
        std::max(0, bounds.height() - static_cast<int>(rows - 1) *
                                          visual_style::kSidebarSplitPaneGap);
    const size_t row = segment / kColumns;
    const size_t column = segment % kColumns;
    const int x = bounds.x() + static_cast<int>(column) *
                                   (content_width / kColumns +
                                    visual_style::kSidebarSplitPaneGap);
    const int y = bounds.y() + static_cast<int>(row) *
                                   (content_height / static_cast<int>(rows) +
                                    visual_style::kSidebarSplitPaneGap);
    const int right =
        bounds.x() + static_cast<int>(column + 1) * content_width / kColumns +
        static_cast<int>(column) * visual_style::kSidebarSplitPaneGap;
    const int bottom =
        bounds.y() +
        static_cast<int>(row + 1) * content_height / static_cast<int>(rows) +
        static_cast<int>(row) * visual_style::kSidebarSplitPaneGap;
    return gfx::Rect(x, y, std::max(0, right - x), std::max(0, bottom - y));
  }

  const bool side_by_side =
      visual_data.split_layout() == split_tabs::SplitTabLayout::kSideBySide;
  const bool main_at_start =
      visual_data.arrangement() == split_tabs::SplitTabArrangement::kMainStart;
  if (visual_data.arrangement() == split_tabs::SplitTabArrangement::kLinear) {
    if (side_by_side) {
      const int first_width =
          SplitAt(bounds.width(), visual_data.split_ratio(), kGap);
      const int remaining_width =
          std::max(0, bounds.width() - first_width - 2 * kGap);
      const int second_width =
          SplitAt(remaining_width, visual_data.secondary_split_ratio(), 0);
      if (segment == 0) {
        return gfx::Rect(bounds.x(), bounds.y(), first_width, bounds.height());
      }
      if (segment == 1) {
        return gfx::Rect(bounds.x() + first_width + kGap, bounds.y(),
                         second_width, bounds.height());
      }
      return gfx::Rect(bounds.x() + first_width + kGap + second_width + kGap,
                       bounds.y(), std::max(0, remaining_width - second_width),
                       bounds.height());
    }
    const int first_height =
        SplitAt(bounds.height(), visual_data.split_ratio(), kGap);
    const int remaining_height =
        std::max(0, bounds.height() - first_height - 2 * kGap);
    const int second_height =
        SplitAt(remaining_height, visual_data.secondary_split_ratio(), 0);
    if (segment == 0) {
      return gfx::Rect(bounds.x(), bounds.y(), bounds.width(), first_height);
    }
    if (segment == 1) {
      return gfx::Rect(bounds.x(), bounds.y() + first_height + kGap,
                       bounds.width(), second_height);
    }
    return gfx::Rect(
        bounds.x(), bounds.y() + first_height + kGap + second_height + kGap,
        bounds.width(), std::max(0, remaining_height - second_height));
  }

  if (side_by_side) {
    const int main_width =
        SplitAt(bounds.width(), visual_data.split_ratio(), kGap);
    const int remainder_x =
        main_at_start ? bounds.x() + main_width + kGap : bounds.x();
    const int remainder_width = std::max(0, bounds.width() - main_width - kGap);
    const int main_x =
        main_at_start ? bounds.x() : remainder_x + remainder_width + kGap;
    if (segment == 0) {
      return gfx::Rect(main_x, bounds.y(), main_width, bounds.height());
    }
    const int first_height =
        SplitAt(bounds.height(), visual_data.secondary_split_ratio(), kGap);
    return segment == 1
               ? gfx::Rect(remainder_x, bounds.y(), remainder_width,
                           first_height)
               : gfx::Rect(remainder_x, bounds.y() + first_height + kGap,
                           remainder_width,
                           std::max(0, bounds.height() - first_height - kGap));
  }

  const int main_height =
      SplitAt(bounds.height(), visual_data.split_ratio(), kGap);
  const int remainder_y =
      main_at_start ? bounds.y() + main_height + kGap : bounds.y();
  const int remainder_height =
      std::max(0, bounds.height() - main_height - kGap);
  const int main_y =
      main_at_start ? bounds.y() : remainder_y + remainder_height + kGap;
  if (segment == 0) {
    return gfx::Rect(bounds.x(), main_y, bounds.width(), main_height);
  }
  const int first_width =
      SplitAt(bounds.width(), visual_data.secondary_split_ratio(), kGap);
  return segment == 1
             ? gfx::Rect(bounds.x(), remainder_y, first_width, remainder_height)
             : gfx::Rect(bounds.x() + first_width + kGap, remainder_y,
                         std::max(0, bounds.width() - first_width - kGap),
                         remainder_height);
}

}  // namespace ahoi::sidebar
