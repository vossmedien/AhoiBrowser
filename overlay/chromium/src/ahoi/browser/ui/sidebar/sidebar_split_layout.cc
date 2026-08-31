// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "ahoi/browser/ui/visual_style.h"

namespace ahoi::sidebar {

namespace {

constexpr int kGap = visual_style::kSidebarSplitPaneGap;
constexpr int kDividerHitThickness = 10;

int SplitAt(int extent, double ratio, int gap) {
  const int available_extent = std::max(0, extent - gap);
  return std::clamp(static_cast<int>(available_extent * ratio), 0,
                    available_extent);
}

bool IsVertical(const SidebarSplitSeparator& separator) {
  return separator.start.x() == separator.end.x();
}

std::optional<SidebarSplitDivider> MergeSeparators(
    const std::vector<SidebarSplitSeparator>& separators,
    bool vertical,
    size_t divider_index,
    double ratio,
    int ratio_extent,
    bool reverse_ratio_direction) {
  std::optional<float> coordinate;
  float leading = 0.0f;
  float trailing = 0.0f;
  for (const SidebarSplitSeparator& separator : separators) {
    if (IsVertical(separator) != vertical) {
      continue;
    }
    const float candidate =
        vertical ? separator.start.x() : separator.start.y();
    if (!coordinate.has_value()) {
      coordinate = candidate;
      leading = vertical ? std::min(separator.start.y(), separator.end.y())
                         : std::min(separator.start.x(), separator.end.x());
      trailing = vertical ? std::max(separator.start.y(), separator.end.y())
                          : std::max(separator.start.x(), separator.end.x());
      continue;
    }
    // This helper merges only collinear pieces. Callers split linear layouts
    // by coordinate first, while grid/main layouts deliberately share one
    // logical ratio across every collinear piece.
    if (std::abs(candidate - *coordinate) > 0.5f) {
      continue;
    }
    leading = std::min(
        leading, vertical ? std::min(separator.start.y(), separator.end.y())
                          : std::min(separator.start.x(), separator.end.x()));
    trailing = std::max(
        trailing, vertical ? std::max(separator.start.y(), separator.end.y())
                           : std::max(separator.start.x(), separator.end.x()));
  }
  if (!coordinate.has_value() || trailing <= leading || ratio_extent <= 0) {
    return std::nullopt;
  }
  return SidebarSplitDivider{
      .divider_index = divider_index,
      .start = vertical ? gfx::PointF(*coordinate, leading)
                        : gfx::PointF(leading, *coordinate),
      .end = vertical ? gfx::PointF(*coordinate, trailing)
                      : gfx::PointF(trailing, *coordinate),
      .ratio = ratio,
      .ratio_extent = ratio_extent,
      .reverse_ratio_direction = reverse_ratio_direction};
}

int AxisExtent(const gfx::Rect& bounds, bool horizontal) {
  return horizontal ? bounds.width() : bounds.height();
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

std::vector<SidebarSplitDivider> GetSidebarSplitDividers(
    const gfx::Rect& group_bounds,
    const std::vector<gfx::Rect>& segment_bounds,
    const gfx::RectF& paint_bounds,
    const split_tabs::SplitTabVisualData& visual_data) {
  std::vector<SidebarSplitDivider> dividers;
  const size_t pane_count = segment_bounds.size();
  if (pane_count < 2 || pane_count > 4 || group_bounds.IsEmpty()) {
    return dividers;
  }

  const std::vector<SidebarSplitSeparator> separators =
      GetSidebarSplitSeparators(segment_bounds, paint_bounds);
  const bool primary_vertical =
      visual_data.split_layout() == split_tabs::SplitTabLayout::kSideBySide;
  if (pane_count == 2) {
    if (auto divider = MergeSeparators(
            separators, primary_vertical, 0, visual_data.split_ratio(),
            std::max(0, AxisExtent(group_bounds, primary_vertical) - kGap),
            /*reverse_ratio_direction=*/false)) {
      dividers.push_back(*divider);
    }
    return dividers;
  }

  if (pane_count == 3 &&
      visual_data.arrangement() == split_tabs::SplitTabArrangement::kLinear) {
    std::vector<float> coordinates;
    for (const SidebarSplitSeparator& separator : separators) {
      if (IsVertical(separator) == primary_vertical) {
        coordinates.push_back(primary_vertical ? separator.start.x()
                                               : separator.start.y());
      }
    }
    std::ranges::sort(coordinates);
    coordinates.erase(
        std::unique(coordinates.begin(), coordinates.end(),
                    [](float a, float b) { return std::abs(a - b) <= 0.5f; }),
        coordinates.end());
    for (size_t index = 0; index < std::min<size_t>(2, coordinates.size());
         ++index) {
      std::vector<SidebarSplitSeparator> collinear;
      for (const SidebarSplitSeparator& separator : separators) {
        const float coordinate =
            primary_vertical ? separator.start.x() : separator.start.y();
        if (IsVertical(separator) == primary_vertical &&
            std::abs(coordinate - coordinates[index]) <= 0.5f) {
          collinear.push_back(separator);
        }
      }
      const int ratio_extent =
          index == 0
              ? std::max(0,
                         AxisExtent(group_bounds, primary_vertical) - 2 * kGap)
              : std::max(0,
                         AxisExtent(segment_bounds[1], primary_vertical) +
                             AxisExtent(segment_bounds[2], primary_vertical));
      if (auto divider = MergeSeparators(
              collinear, primary_vertical, index,
              index == 0 ? visual_data.split_ratio()
                         : visual_data.secondary_split_ratio(),
              ratio_extent, /*reverse_ratio_direction=*/false)) {
        dividers.push_back(*divider);
      }
    }
    return dividers;
  }

  const bool primary_reversed =
      pane_count == 3 &&
      visual_data.arrangement() == split_tabs::SplitTabArrangement::kMainEnd;
  if (auto primary = MergeSeparators(
          separators, primary_vertical, 0, visual_data.split_ratio(),
          std::max(0, AxisExtent(group_bounds, primary_vertical) - kGap),
          primary_reversed)) {
    dividers.push_back(*primary);
  }
  if (auto secondary = MergeSeparators(
          separators, !primary_vertical, 1, visual_data.secondary_split_ratio(),
          std::max(0, AxisExtent(group_bounds, !primary_vertical) - kGap),
          /*reverse_ratio_direction=*/false)) {
    dividers.push_back(*secondary);
  }
  return dividers;
}

gfx::Rect GetSidebarSplitDividerHitBounds(const SidebarSplitDivider& divider) {
  const bool vertical = divider.resizes_horizontally();
  const int leading = static_cast<int>(
      std::floor(vertical ? std::min(divider.start.y(), divider.end.y())
                          : std::min(divider.start.x(), divider.end.x())));
  const int trailing = static_cast<int>(
      std::ceil(vertical ? std::max(divider.start.y(), divider.end.y())
                         : std::max(divider.start.x(), divider.end.x())));
  const int coordinate = static_cast<int>(
      std::round(vertical ? divider.start.x() : divider.start.y()));
  return vertical
             ? gfx::Rect(coordinate - kDividerHitThickness / 2, leading,
                         kDividerHitThickness, std::max(0, trailing - leading))
             : gfx::Rect(leading, coordinate - kDividerHitThickness / 2,
                         std::max(0, trailing - leading), kDividerHitThickness);
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
    // Match MultiContentsView's ratio-aware 2x2 geometry. Pane identity stays
    // row-major in the sidebar (0/1 above 2/3), while the primary layout axis
    // and shared secondary ratio mirror the live WebContents grid.
    const int content_width = std::max(0, bounds.width() - kGap);
    const int content_height = std::max(0, bounds.height() - kGap);
    const int leading_width =
        SplitAt(bounds.width(), visual_data.split_ratio(), kGap);
    const int leading_height =
        SplitAt(bounds.height(), visual_data.split_ratio(), kGap);
    const int secondary_width =
        SplitAt(bounds.width(), visual_data.secondary_split_ratio(), kGap);
    const int secondary_height =
        SplitAt(bounds.height(), visual_data.secondary_split_ratio(), kGap);
    const bool side_by_side =
        visual_data.split_layout() == split_tabs::SplitTabLayout::kSideBySide;
    const size_t row = segment / 2;
    const size_t column = segment % 2;
    const int first_width = side_by_side ? leading_width : secondary_width;
    const int first_height = side_by_side ? secondary_height : leading_height;
    const int x = column == 0 ? bounds.x() : bounds.x() + first_width + kGap;
    const int y = row == 0 ? bounds.y() : bounds.y() + first_height + kGap;
    const int width = column == 0 ? first_width : content_width - first_width;
    const int height = row == 0 ? first_height : content_height - first_height;
    return gfx::Rect(x, y, std::max(0, width), std::max(0, height));
  }

  const bool side_by_side =
      visual_data.split_layout() == split_tabs::SplitTabLayout::kSideBySide;
  const bool main_at_start =
      visual_data.arrangement() == split_tabs::SplitTabArrangement::kMainStart;
  if (visual_data.arrangement() == split_tabs::SplitTabArrangement::kLinear) {
    if (side_by_side) {
      const int first_width =
          SplitAt(bounds.width(), visual_data.split_ratio(), 2 * kGap);
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
        SplitAt(bounds.height(), visual_data.split_ratio(), 2 * kGap);
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
