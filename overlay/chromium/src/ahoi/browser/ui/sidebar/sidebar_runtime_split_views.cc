// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_resize_area.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {
namespace {

// Paints the non-interactive group chrome above the pane rows. Keeping the
// outline and separators in a final child prevents a selected or hovered pane
// background from erasing the visual boundary of the complete split.
class OpenTabSplitChromeView final : public views::View {
  METADATA_HEADER(OpenTabSplitChromeView, views::View)

 public:
  OpenTabSplitChromeView(size_t pane_count,
                         const split_tabs::SplitTabVisualData& visual_data)
      : pane_count_(pane_count), visual_data_(visual_data) {
    CHECK_GE(pane_count_, 2u);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
  }

  OpenTabSplitChromeView(const OpenTabSplitChromeView&) = delete;
  OpenTabSplitChromeView& operator=(const OpenTabSplitChromeView&) = delete;
  ~OpenTabSplitChromeView() override = default;

  void SetVisualData(const split_tabs::SplitTabVisualData& visual_data) {
    visual_data_ = visual_data;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const ui::ColorProvider* const colors = GetColorProvider();
    if (!colors || width() <= 0 || height() <= 0) {
      return;
    }

    gfx::RectF group_bounds(GetLocalBounds());
    group_bounds.Inset(
        gfx::InsetsF::VH(visual_style::kSidebarTabRowVerticalInset,
                         visual_style::kSidebarTabRowHorizontalInset));
    if (group_bounds.IsEmpty()) {
      return;
    }

    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setColor(colors->GetColor(visual_style::kDivider));
    outline.setStrokeWidth(1.0f);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    gfx::RectF outline_bounds = group_bounds;
    outline_bounds.Inset(outline.getStrokeWidth() / 2.0f);
    canvas->DrawRoundRect(outline_bounds,
                          std::max(0.0f, visual_style::kRowCornerRadius -
                                             outline.getStrokeWidth() / 2.0f),
                          outline);

    std::vector<gfx::Rect> pane_bounds;
    pane_bounds.reserve(pane_count_);
    const gfx::Rect bounds = GetLocalBounds();
    for (size_t pane = 0; pane < pane_count_; ++pane) {
      pane_bounds.push_back(
          GetSplitSegmentBounds(bounds, pane, pane_count_, visual_data_));
    }

    cc::PaintFlags separator = outline;
    gfx::RectF separator_bounds = group_bounds;
    separator_bounds.Inset(separator.getStrokeWidth() / 2.0f);
    for (const SidebarSplitSeparator& split_separator :
         GetSidebarSplitSeparators(pane_bounds, separator_bounds)) {
      canvas->DrawLine(split_separator.start, split_separator.end, separator);
    }
  }

 private:
  const size_t pane_count_;
  split_tabs::SplitTabVisualData visual_data_;
};

BEGIN_METADATA(OpenTabSplitChromeView)
END_METADATA

// A live Chromium split is one visual row in the sidebar as well. Temporary
// panes and mixed saved/temporary collections live in this composite runtime
// representation, following SplitTabData rather than inferring membership
// from adjacency in TabStripModel.
class OpenTabSplitRowView final : public views::View {
  METADATA_HEADER(OpenTabSplitRowView, views::View)

 public:
  OpenTabSplitRowView(std::vector<std::unique_ptr<views::View>> tabs,
                      split_tabs::SplitTabVisualData visual_data,
                      SidebarSplitResizeCallback resize_callback)
      : visual_data_(std::move(visual_data)),
        resize_callback_(std::move(resize_callback)) {
    CHECK_GE(tabs.size(), 2u);
    SetPreferredSize(gfx::Size(
        0, GetSplitRowPreferredHeight(tabs.size(), visual_data_,
                                      SidebarTreeRowView::kRowHeight)));
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    for (auto& tab : tabs) {
      views::View* const pane = AddChildView(std::move(tab));
      CHECK(SetOpenTabSplitSegmentPresentation(pane));
      pane_views_.push_back(pane);
    }
    chrome_overlay_ = AddChildView(std::make_unique<OpenTabSplitChromeView>(
        pane_views_.size(), visual_data_));
  }

  OpenTabSplitRowView(const OpenTabSplitRowView&) = delete;
  OpenTabSplitRowView& operator=(const OpenTabSplitRowView&) = delete;
  ~OpenTabSplitRowView() override = default;

  void Layout(PassKey) override {
    const int count = static_cast<int>(pane_views_.size());
    if (count == 0) {
      return;
    }
    const gfx::Rect bounds = GetContentsBounds();
    std::vector<gfx::Rect> pane_bounds;
    pane_bounds.reserve(count);
    for (int index = 0; index < count; ++index) {
      pane_bounds.push_back(
          GetSplitSegmentBounds(bounds, index, count, visual_data_));
      pane_views_[index]->SetBoundsRect(pane_bounds.back());
    }
    auto* const chrome =
        views::AsViewClass<OpenTabSplitChromeView>(chrome_overlay_);
    CHECK(chrome);
    chrome->SetVisualData(visual_data_);
    chrome_overlay_->SetBoundsRect(bounds);

    gfx::RectF paint_bounds(bounds);
    paint_bounds.Inset(
        gfx::InsetsF::VH(visual_style::kSidebarTabRowVerticalInset,
                         visual_style::kSidebarTabRowHorizontalInset));
    SkPathBuilder clip_builder;
    clip_builder.addRRect(SkRRect::MakeRectXY(gfx::RectFToSkRect(paint_bounds),
                                              visual_style::kRowCornerRadius,
                                              visual_style::kRowCornerRadius));
    SetClipPath(clip_builder.detach());

    const std::vector<SidebarSplitDivider> dividers = GetSidebarSplitDividers(
        bounds, pane_bounds, paint_bounds, visual_data_);
    while (resize_areas_.size() < dividers.size()) {
      resize_areas_.push_back(
          AddChildView(std::make_unique<SidebarSplitResizeArea>(
              dividers[resize_areas_.size()],
              base::BindRepeating(&OpenTabSplitRowView::ResizeSplit,
                                  base::Unretained(this)))));
    }
    while (resize_areas_.size() > dividers.size() &&
           !resize_areas_.back()->is_resizing()) {
      RemoveChildViewT(resize_areas_.back());
      resize_areas_.pop_back();
    }
    for (size_t index = 0;
         index < std::min(resize_areas_.size(), dividers.size()); ++index) {
      resize_areas_[index]->UpdateConfiguration(
          dividers[index],
          base::BindRepeating(&OpenTabSplitRowView::ResizeSplit,
                              base::Unretained(this)));
      resize_areas_[index]->SetBoundsRect(
          GetSidebarSplitDividerHitBounds(dividers[index]));
      resize_areas_[index]->SetVisible(!resize_callback_.is_null());
      ReorderChildView(resize_areas_[index], children().size() - 1u);
    }
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    views::View::OnPaintBackground(canvas);
    const ui::ColorProvider* const colors = GetColorProvider();
    if (!colors || width() <= 0 || height() <= 0) {
      return;
    }
    gfx::RectF background(GetLocalBounds());
    background.Inset(
        gfx::InsetsF::VH(visual_style::kSidebarTabRowVerticalInset,
                         visual_style::kSidebarTabRowHorizontalInset));
    if (background.IsEmpty()) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(colors->GetColor(visual_style::kRaisedSurface));
    fill.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(background, visual_style::kRowCornerRadius, fill);
  }

 private:
  bool ResizeSplit(size_t divider_index, double ratio, bool done_resizing) {
    if (!resize_callback_ ||
        !resize_callback_.Run(divider_index, ratio, done_resizing)) {
      return false;
    }
    const bool accepted = divider_index == 0
                              ? visual_data_.set_split_ratio(ratio)
                              : visual_data_.set_secondary_split_ratio(ratio);
    if (!accepted) {
      return false;
    }
    InvalidateLayout();
    SchedulePaint();
    return true;
  }

  split_tabs::SplitTabVisualData visual_data_;
  const SidebarSplitResizeCallback resize_callback_;
  std::vector<raw_ptr<views::View>> pane_views_;
  raw_ptr<views::View> chrome_overlay_ = nullptr;
  std::vector<raw_ptr<SidebarSplitResizeArea>> resize_areas_;
};

BEGIN_METADATA(OpenTabSplitRowView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateOpenTabSplitRowView(
    std::vector<std::unique_ptr<views::View>> tabs,
    split_tabs::SplitTabVisualData visual_data,
    SidebarSplitResizeCallback resize_callback) {
  return std::make_unique<OpenTabSplitRowView>(
      std::move(tabs), std::move(visual_data), std::move(resize_callback));
}

}  // namespace ahoi::sidebar
