// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/i18n/rtl.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"

namespace ahoi::sidebar {

namespace {

constexpr int kLeadingPadding = 8;
constexpr int kDisclosureWidth = 16;
constexpr int kIconSize = 16;
constexpr int kIconTitleSpacing = 6;
constexpr int kTrailingActionSize = 24;
constexpr float kDropStrokeWidth = 2.0f;
constexpr float kSelectedDotRadius = 3.0f;

cc::PaintFlags FillFlags(SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  return flags;
}

cc::PaintFlags StrokeFlags(SkColor color, float width) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStrokeWidth(width);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  return flags;
}

bool IsCustomGroupIcon(std::u16string_view icon) {
  return !icon.empty() && icon != u"folder" && icon != u"code" &&
         icon != u"lock" && icon != u"archive" && icon != u"moon";
}

}  // namespace

SidebarTreeRowView::SidebarTreeRowView(SidebarTreeView* owner,
                                       std::u16string split_with_prefix)
    : owner_(owner), split_with_prefix_(std::move(split_with_prefix)) {
  CHECK(owner_);
  CHECK(!split_with_prefix_.empty());
  chevron_animation_.SetSlideDuration(visual_style::kTreeMotionDuration);
  set_context_menu_controller(owner_);
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  SetNotifyEnterExitOnChild(true);

  title_label_ = AddChildView(std::make_unique<views::Label>());
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetEnabledColor(visual_style::kText);
  title_label_->SetCanProcessEventsWithinSubtree(false);
  title_label_->GetViewAccessibility().SetIsIgnored(true);

  editor_ = AddChildView(std::make_unique<views::Textfield>());
  editor_->SetController(this);
  editor_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  editor_->SetVisible(false);
  editor_->SetBackgroundColor(visual_style::kRaisedSurface);

  GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
}

SidebarTreeRowView::~SidebarTreeRowView() {
  set_context_menu_controller(nullptr);
}

void SidebarTreeRowView::Bind(size_t row_index,
                              const SidebarTreeViewModel::Row& row,
                              const tab_tree::TreeNode& node,
                              bool selected,
                              size_t split_segment_index,
                              size_t split_segment_count,
                              ui::ImageModel page_icon,
                              ui::ImageModel media_indicator,
                              std::u16string status_text,
                              std::vector<gfx::ImageSkia> drag_thumbnails,
                              bool running,
                              bool sleeping) {
  CHECK_EQ(row.node_id, node.id);
  CHECK_GT(split_segment_count, 0u);
  CHECK_LT(split_segment_index, split_segment_count);
  const bool same_node = node_id_ == row.node_id;
  const bool title_changed = !same_node || title_ != node.title;
  const bool media_presence_changed =
      media_indicator_.IsEmpty() != media_indicator.IsEmpty();
  const bool layout_changed = !same_node || depth_ != row.depth ||
                              type_ != row.type ||
                              split_segment_index_ != split_segment_index ||
                              split_segment_count_ != split_segment_count ||
                              title_changed || media_presence_changed;
  const bool expanded_changed = same_node &&
                                type_ == tab_tree::TreeNodeType::kFolder &&
                                expanded_ != row.expanded;
  row_index_ = row_index;
  node_id_ = row.node_id;
  depth_ = row.depth;
  position_in_parent_ = row.position_in_parent;
  sibling_count_ = row.sibling_count;
  type_ = row.type;
  title_ = node.title;
  folder_icon_ = node.icon;
  accent_argb_ = node.accent_argb;
  page_icon_ = std::move(page_icon);
  media_indicator_ = std::move(media_indicator);
  status_text_ = std::move(status_text);
  drag_thumbnails_ = std::move(drag_thumbnails);
  expanded_ = row.expanded;
  if (!same_node) {
    chevron_animation_.Reset(expanded_ ? 1.0 : 0.0);
  } else if (!gfx::Animation::ShouldRenderRichAnimation()) {
    chevron_animation_.Reset(expanded_ ? 1.0 : 0.0);
  } else if (expanded_changed && expanded_) {
    chevron_animation_.Show();
  } else if (expanded_changed) {
    chevron_animation_.Hide();
  }
  selected_ = selected;
  running_ = running;
  sleeping_ = sleeping;
  split_segment_index_ = split_segment_index;
  split_segment_count_ = split_segment_count;
  if (title_changed && !is_editing_) {
    title_label_->SetText(
        split_drop_target_ ? split_with_prefix_ + u" " + title_ : title_);
    editor_->SetText(title_);
  }
  UpdateAccessibility();
  // Bind() runs from SidebarTreeView::Layout(). Re-invalidating an unchanged
  // recycled row here recursively schedules another full BrowserView layout.
  // Besides wasting a frame for every materialized row, a continuously
  // loading page could keep the browser and GPU in a permanent repaint loop.
  // Only geometry-affecting changes need another child layout pass.
  if (layout_changed) {
    InvalidateLayout();
  }
  SchedulePaint();
}

void SidebarTreeRowView::Unbind() {
  if (hovered_ && is_bound()) {
    owner_->OnRowHoverChanged(this, false);
  }
  StopEditing(/*restore_model_title=*/true);
  // A row can be recycled while its source drag tears down a split
  // projection. Reset child visibility explicitly; merely clearing
  // `dragging_` would otherwise leave the next bound title hidden.
  title_label_->SetVisible(true);
  editor_->SetVisible(false);
  node_id_ = base::Uuid();
  title_.clear();
  folder_icon_.clear();
  accent_argb_.reset();
  drop_position_.reset();
  split_drop_target_ = false;
  selected_ = false;
  hovered_ = false;
  running_ = false;
  sleeping_ = false;
  page_icon_ = ui::ImageModel();
  media_indicator_ = ui::ImageModel();
  status_text_.clear();
  drag_thumbnails_.clear();
  dragging_ = false;
  pressed_disclosure_ = false;
  pressed_trailing_action_ = false;
  split_segment_index_ = 0;
  split_segment_count_ = 1;
  GetViewAccessibility().SetIsInvisible(true);
}

bool SidebarTreeRowView::title_visible_for_testing() const {
  return title_label_->GetVisible();
}

gfx::Rect SidebarTreeRowView::title_bounds_for_testing() const {
  return GetMirroredRect(TitleBounds());
}

void SidebarTreeRowView::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  title_label_->SetEnabledColor(visual_style::kText);
  GetViewAccessibility().SetIsSelected(selected_);
  SchedulePaint();
}

void SidebarTreeRowView::SetDropPosition(
    std::optional<SidebarTreeController::DropPosition> position) {
  if (drop_position_ == position) {
    return;
  }
  drop_position_ = position;
  SchedulePaint();
}

void SidebarTreeRowView::SetSplitDropTarget(bool split_drop_target) {
  if (split_drop_target_ == split_drop_target) {
    return;
  }
  split_drop_target_ = split_drop_target;
  title_label_->SetText(split_drop_target_ ? split_with_prefix_ + u" " + title_
                                           : title_);
  UpdateAccessibility();
  InvalidateLayout();
  SchedulePaint();
}

gfx::ImageSkia SidebarTreeRowView::GetDragImage() {
  CHECK(is_bound());
  const ui::ColorProvider* const colors = GetColorProvider();
  gfx::ImageSkia favicon;
  // Vector and generator ImageModels require a ColorProvider. A native drag
  // can cross the Views lifecycle boundary before the row is attached to a
  // widget, so rasterize only provider-independent bitmap models in that
  // state. The drag card paints its own neutral fallback icon otherwise.
  if (!page_icon_.IsEmpty() && (colors || page_icon_.IsImage())) {
    favicon = page_icon_.Rasterize(colors);
  }
  std::vector<gfx::ImageSkia> live_thumbnails =
      owner_->GetSavedPageDragThumbnailsForNode(node_id_);
  if (live_thumbnails.empty()) {
    live_thumbnails = drag_thumbnails_;
  }
  return CreateSidebarDragImage(GetWidget(), colors, favicon, title_,
                                live_thumbnails);
}

void SidebarTreeRowView::SetIsDragging(bool dragging) {
  if (dragging_ == dragging) {
    return;
  }
  dragging_ = dragging;
  title_label_->SetVisible(!dragging_ && !is_editing_);
  SchedulePaint();
}

void SidebarTreeRowView::StartEditing() {
  if (!is_bound() || is_editing_) {
    return;
  }
  is_editing_ = true;
  editor_->SetText(title_);
  editor_->SetVisible(true);
  title_label_->SetVisible(false);
  editor_->SelectAll(/*reversed=*/false);
  editor_->RequestFocus();
}

void SidebarTreeRowView::StopEditing(bool restore_model_title) {
  if (!is_editing_) {
    return;
  }
  is_editing_ = false;
  if (restore_model_title) {
    editor_->SetText(title_);
  }
  title_label_->SetText(title_);
  editor_->SetVisible(false);
  title_label_->SetVisible(!dragging_);
}

std::u16string SidebarTreeRowView::editor_text_for_testing() const {
  return std::u16string(editor_->GetText());
}

void SidebarTreeRowView::Layout(PassKey) {
  const gfx::Rect title_bounds = GetMirroredRect(TitleBounds());
  title_label_->SetBoundsRect(title_bounds);
  editor_->SetBoundsRect(title_bounds);
}

void SidebarTreeRowView::OnPaintBackground(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  gfx::RectF background(GetLocalBounds());
  background.Inset(gfx::InsetsF::VH(
      visual_style::kSidebarTabRowVerticalInset,
      split_segment_count_ > 1 ? visual_style::kSidebarSplitPaneHorizontalInset
                               : visual_style::kSidebarTabRowHorizontalInset));

  if (dragging_) {
    canvas->DrawRoundRect(
        background, visual_style::kRowCornerRadius,
        FillFlags(colors->GetColor(visual_style::kHoverSurface)));
  } else if (split_drop_target_ ||
             drop_position_ == SidebarTreeController::DropPosition::kInside) {
    canvas->DrawRoundRect(
        background, visual_style::kRowCornerRadius,
        FillFlags(colors->GetColor(visual_style::kDropTargetSurface)));
  } else if (selected_) {
    canvas->DrawRoundRect(
        background, visual_style::kRowCornerRadius,
        FillFlags(colors->GetColor(visual_style::kSelectedSurface)));
  } else if (hovered_) {
    canvas->DrawRoundRect(
        background, visual_style::kRowCornerRadius,
        FillFlags(colors->GetColor(visual_style::kHoverSurface)));
  }

  if (drop_position_ == SidebarTreeController::DropPosition::kBefore ||
      drop_position_ == SidebarTreeController::DropPosition::kAfter) {
    // Paint the complete effective edge zone, not merely its insertion line.
    // This makes the generous hit target visible and therefore aimable while
    // keeping row geometry absolutely stable during the native drag.
    const float zone_height = static_cast<float>(
        std::clamp(height() * 3 / 10, 1, std::max(1, (height() - 1) / 2)));
    gfx::RectF zone(
        visual_style::kSidebarTabRowHorizontalInset, 0.0f,
        std::max(0, width() - 2 * visual_style::kSidebarTabRowHorizontalInset),
        zone_height);
    if (drop_position_ == SidebarTreeController::DropPosition::kAfter) {
      zone.set_y(std::max(0.0f, static_cast<float>(height()) - zone_height));
    }
    canvas->DrawRoundRect(
        zone, visual_style::kRowCornerRadius,
        FillFlags(colors->GetColor(visual_style::kDropTargetSurface)));
  }

  if (split_drop_target_) {
    gfx::RectF outline = background;
    outline.Inset(kDropStrokeWidth / 2.0f);
    cc::PaintFlags split_outline =
        StrokeFlags(colors->GetColor(visual_style::kAccent), kDropStrokeWidth);
    constexpr float kDashIntervals[] = {5.0f, 3.0f};
    split_outline.setPathEffect(
        cc::PathEffect::MakeDash(kDashIntervals, 2, 0.0f));
    canvas->DrawRoundRect(
        outline, visual_style::kRowCornerRadius - kDropStrokeWidth / 2.0f,
        split_outline);
  } else if (drop_position_ == SidebarTreeController::DropPosition::kInside) {
    gfx::RectF outline = background;
    outline.Inset(kDropStrokeWidth / 2.0f);
    canvas->DrawRoundRect(
        outline, visual_style::kRowCornerRadius - kDropStrokeWidth / 2.0f,
        StrokeFlags(colors->GetColor(visual_style::kAccent), kDropStrokeWidth));
  }

  if (selected_ && owner_->HasFocus()) {
    gfx::RectF focus = background;
    focus.Inset(1.0f);
    canvas->DrawRoundRect(
        focus, visual_style::kRowCornerRadius - 1.0f,
        StrokeFlags(colors->GetColor(visual_style::kFocusRing), 1.0f));
  }
}

void SidebarTreeRowView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (dragging_) {
    return;
  }
  const bool paint_trailing_state = ShouldPaintTrailingState();
  const SkColor icon_color = GetColorProvider()->GetColor(
      selected_ ? visual_style::kText : visual_style::kMutedText);

  if (is_folder()) {
    const gfx::Rect disclosure = GetMirroredRect(DisclosureBounds());
    const gfx::Point center = disclosure.CenterPoint();
    const double expanded_progress = chevron_animation_.is_animating()
                                         ? chevron_animation_.GetCurrentValue()
                                         : (expanded_ ? 1.0 : 0.0);
    const bool rtl = base::i18n::IsRTL();
    const gfx::PointF start_top(center.x() + (rtl ? 2.0f : -2.0f),
                                center.y() - 4.0f);
    const gfx::PointF start_middle(center.x() + (rtl ? -2.0f : 2.0f),
                                   center.y());
    const gfx::PointF start_bottom(center.x() + (rtl ? 2.0f : -2.0f),
                                   center.y() + 4.0f);
    const gfx::PointF end_left(center.x() - 4.0f, center.y() - 2.0f);
    const gfx::PointF end_middle(center.x(), center.y() + 2.0f);
    const gfx::PointF end_right(center.x() + 4.0f, center.y() - 2.0f);
    const auto interpolate = [expanded_progress](const gfx::PointF& from,
                                                 const gfx::PointF& to) {
      return gfx::PointF(from.x() + (to.x() - from.x()) * expanded_progress,
                         from.y() + (to.y() - from.y()) * expanded_progress);
    };
    const gfx::PointF top = interpolate(start_top, end_left);
    const gfx::PointF middle = interpolate(start_middle, end_middle);
    const gfx::PointF bottom = interpolate(start_bottom, end_right);
    SkPathBuilder chevron;
    chevron.moveTo(top.x(), top.y());
    chevron.lineTo(middle.x(), middle.y());
    chevron.lineTo(bottom.x(), bottom.y());
    canvas->DrawPath(chevron.detach(), StrokeFlags(icon_color, 1.5f));

    const gfx::Rect icon_bounds = GetMirroredRect(IconBounds());
    const SkColor folder_color = accent_argb_.has_value()
                                     ? static_cast<SkColor>(*accent_argb_)
                                     : icon_color;
    const cc::PaintFlags folder_stroke = StrokeFlags(folder_color, 1.5f);
    const gfx::Point icon_center = icon_bounds.CenterPoint();
    if (IsCustomGroupIcon(folder_icon_)) {
      canvas->DrawStringRectWithFlags(
          folder_icon_,
          title_label_->font_list().DeriveWithHeightUpperBound(kIconSize),
          folder_color, icon_bounds,
          gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_ELLIPSIS);
    } else if (folder_icon_ == u"code") {
      canvas->DrawLine(
          gfx::PointF(icon_center.x() - 1.0f, icon_center.y() - 4.0f),
          gfx::PointF(icon_center.x() - 5.0f, icon_center.y()), folder_stroke);
      canvas->DrawLine(
          gfx::PointF(icon_center.x() - 5.0f, icon_center.y()),
          gfx::PointF(icon_center.x() - 1.0f, icon_center.y() + 4.0f),
          folder_stroke);
      canvas->DrawLine(
          gfx::PointF(icon_center.x() + 1.0f, icon_center.y() - 4.0f),
          gfx::PointF(icon_center.x() + 5.0f, icon_center.y()), folder_stroke);
      canvas->DrawLine(
          gfx::PointF(icon_center.x() + 5.0f, icon_center.y()),
          gfx::PointF(icon_center.x() + 1.0f, icon_center.y() + 4.0f),
          folder_stroke);
    } else if (folder_icon_ == u"lock") {
      gfx::RectF body(icon_bounds.x() + 3.0f, icon_bounds.y() + 7.0f, 10.0f,
                      7.0f);
      canvas->DrawRoundRect(body, 2.0f, folder_stroke);
      gfx::RectF shackle(icon_bounds.x() + 5.0f, icon_bounds.y() + 2.0f, 6.0f,
                         9.0f);
      canvas->DrawRoundRect(shackle, 3.0f, folder_stroke);
    } else if (folder_icon_ == u"archive") {
      gfx::RectF archive(icon_bounds.x() + 2.0f, icon_bounds.y() + 5.0f, 12.0f,
                         9.0f);
      canvas->DrawRoundRect(archive, 1.5f, folder_stroke);
      canvas->DrawLine(
          gfx::PointF(icon_bounds.x() + 1.0f, icon_bounds.y() + 4.0f),
          gfx::PointF(icon_bounds.right() - 1.0f, icon_bounds.y() + 4.0f),
          folder_stroke);
      canvas->DrawLine(
          gfx::PointF(icon_center.x() - 2.0f, icon_center.y() + 1.0f),
          gfx::PointF(icon_center.x() + 2.0f, icon_center.y() + 1.0f),
          folder_stroke);
    } else if (folder_icon_ == u"moon") {
      SkPathBuilder moon;
      moon.moveTo(icon_center.x() + 2.5f, icon_center.y() - 6.0f);
      moon.cubicTo(icon_center.x() - 4.0f, icon_center.y() - 5.0f,
                   icon_center.x() - 5.0f, icon_center.y() + 4.0f,
                   icon_center.x() + 2.5f, icon_center.y() + 6.0f);
      moon.cubicTo(icon_center.x() - 0.5f, icon_center.y() + 3.0f,
                   icon_center.x() - 0.5f, icon_center.y() - 3.0f,
                   icon_center.x() + 2.5f, icon_center.y() - 6.0f);
      canvas->DrawPath(moon.detach(), folder_stroke);
    } else {
      SkPathBuilder folder;
      folder.moveTo(icon_bounds.x() + 1.0f, icon_bounds.y() + 5.0f);
      folder.lineTo(icon_bounds.x() + 6.0f, icon_bounds.y() + 5.0f);
      folder.lineTo(icon_bounds.x() + 8.0f, icon_bounds.y() + 7.0f);
      folder.lineTo(icon_bounds.right() - 1.0f, icon_bounds.y() + 7.0f);
      folder.lineTo(icon_bounds.right() - 1.0f, icon_bounds.bottom() - 2.0f);
      folder.lineTo(icon_bounds.x() + 1.0f, icon_bounds.bottom() - 2.0f);
      folder.close();
      canvas->DrawPath(folder.detach(), folder_stroke);
    }

  } else {
    const gfx::Rect icon_bounds = GetMirroredRect(IconBounds());
    const gfx::ImageSkia favicon = page_icon_.Rasterize(GetColorProvider());
    if (!favicon.isNull()) {
      const int image_width = std::min(kIconSize, favicon.width());
      const int image_height = std::min(kIconSize, favicon.height());
      canvas->DrawImageInt(favicon, 0, 0, favicon.width(), favicon.height(),
                           icon_bounds.x() + (kIconSize - image_width) / 2,
                           icon_bounds.y() + (kIconSize - image_height) / 2,
                           image_width, image_height, true);
    } else {
      gfx::RectF page_icon(icon_bounds);
      page_icon.Inset(gfx::InsetsF(2.0f));
      canvas->DrawRoundRect(page_icon, 2.0f, StrokeFlags(icon_color, 1.5f));
      cc::PaintFlags line = StrokeFlags(icon_color, 1.0f);
      const float left = page_icon.x() + 3.0f;
      canvas->DrawLine(
          gfx::PointF(left, page_icon.y() + 4.0f),
          gfx::PointF(page_icon.right() - 3.0f, page_icon.y() + 4.0f), line);
    }
  }

  if (split_drop_target_) {
    const float divider_x = static_cast<float>(width()) * 0.5f;
    canvas->DrawLine(
        gfx::PointF(divider_x, 8.0f),
        gfx::PointF(divider_x, std::max(8.0f, height() - 8.0f)),
        StrokeFlags(GetColorProvider()->GetColor(visual_style::kAccent),
                    1.25f));
  }

  if (paint_trailing_state && !ShouldShowTrailingAction() && sleeping_) {
    const gfx::Rect status_bounds = GetMirroredRect(TrailingActionBounds());
    const gfx::Point center = status_bounds.CenterPoint();
    cc::PaintFlags sleep_stroke =
        StrokeFlags(GetColorProvider()->GetColor(visual_style::kAccent), 1.4f);
    SkPathBuilder moon;
    moon.moveTo(center.x() + 3.5f, center.y() - 5.5f);
    moon.cubicTo(center.x() - 3.5f, center.y() - 5.5f, center.x() - 4.5f,
                 center.y() + 4.5f, center.x() + 3.5f, center.y() + 5.5f);
    moon.cubicTo(center.x() - 0.5f, center.y() + 3.5f, center.x() - 0.5f,
                 center.y() - 3.5f, center.x() + 3.5f, center.y() - 5.5f);
    canvas->DrawPath(moon.detach(), sleep_stroke);
  } else if (paint_trailing_state && !ShouldShowTrailingAction() && selected_ &&
             running_ && type_ == tab_tree::TreeNodeType::kSavedPage &&
             split_segment_count_ == 1 && media_indicator_.IsEmpty()) {
    // The accent dot is a live-page indicator, not a generic tree-selection
    // marker. Folders retain their selected surface and AX selection without
    // suggesting that the container itself is an active WebContents.
    const float dot_x = static_cast<float>(width() - 16);
    const float dot_y = static_cast<float>(height()) / 2.0f;
    canvas->DrawCircle(
        gfx::PointF(dot_x, dot_y), kSelectedDotRadius,
        FillFlags(GetColorProvider()->GetColor(visual_style::kAccent)));
  }

  if (paint_trailing_state && !media_indicator_.IsEmpty()) {
    const gfx::Rect indicator_bounds = GetMirroredRect(MediaIndicatorBounds());
    const gfx::ImageSkia indicator =
        media_indicator_.Rasterize(GetColorProvider());
    if (!indicator.isNull()) {
      const int image_width = std::min(kIconSize, indicator.width());
      const int image_height = std::min(kIconSize, indicator.height());
      canvas->DrawImageInt(
          indicator, 0, 0, indicator.width(), indicator.height(),
          indicator_bounds.CenterPoint().x() - image_width / 2,
          indicator_bounds.CenterPoint().y() - image_height / 2, image_width,
          image_height, true);
    }
  }

  if (ShouldShowTrailingAction()) {
    const gfx::Rect action = GetMirroredRect(TrailingActionBounds());
    const gfx::Point center = action.CenterPoint();
    const cc::PaintFlags action_stroke = StrokeFlags(icon_color, 1.5f);
    if (running_) {
      canvas->DrawCircle(gfx::PointF(center), 6.0f, action_stroke);
      // The gap at the top makes this a recognizable power/shutdown symbol.
      canvas->DrawLine(gfx::PointF(center.x(), center.y() - 8.0f),
                       gfx::PointF(center.x(), center.y() - 1.0f),
                       action_stroke);
    } else {
      canvas->DrawLine(gfx::PointF(center.x() - 4.0f, center.y() - 4.0f),
                       gfx::PointF(center.x() + 4.0f, center.y() + 4.0f),
                       action_stroke);
      canvas->DrawLine(gfx::PointF(center.x() + 4.0f, center.y() - 4.0f),
                       gfx::PointF(center.x() - 4.0f, center.y() + 4.0f),
                       action_stroke);
    }
  }
}

bool SidebarTreeRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || !is_bound()) {
    return false;
  }
  const bool disclosure_hit =
      is_folder() &&
      GetMirroredRect(DisclosureBounds()).Contains(event.location());
  pressed_trailing_action_ = IsTrailingActionAt(event.location());
  if (pressed_trailing_action_) {
    return true;
  }
  pressed_disclosure_ = disclosure_hit;
  owner_->OnRowPressed(this, event, disclosure_hit);
  return true;
}

void SidebarTreeRowView::OnMouseReleased(const ui::MouseEvent& event) {
  if (!event.IsLeftMouseButton()) {
    pressed_disclosure_ = false;
    pressed_trailing_action_ = false;
    return;
  }
  const bool trailing_action_hit =
      pressed_trailing_action_ && IsTrailingActionAt(event.location());
  pressed_trailing_action_ = false;
  if (trailing_action_hit) {
    owner_->OnRowTrailingAction(this);
    return;
  }
  const bool disclosure_hit =
      pressed_disclosure_ && is_bound() && is_folder() &&
      GetMirroredRect(DisclosureBounds()).Contains(event.location());
  pressed_disclosure_ = false;
  owner_->OnRowReleased(this, event, disclosure_hit);
}

void SidebarTreeRowView::OnMouseEntered(const ui::MouseEvent& /*event*/) {
  hovered_ = true;
  owner_->OnRowHoverChanged(this, true);
  SchedulePaint();
}

void SidebarTreeRowView::OnMouseExited(const ui::MouseEvent& /*event*/) {
  hovered_ = false;
  owner_->OnRowHoverChanged(this, false);
  SchedulePaint();
}

void SidebarTreeRowView::OnDragDone() {
  SetIsDragging(false);
  owner_->OnRowDragDone();
  views::View::OnDragDone();
}

bool SidebarTreeRowView::HandleAccessibleAction(
    const ui::AXActionData& action_data) {
  if (is_bound()) {
    if (action_data.action == ax::mojom::Action::kFocus) {
      return owner_->OnRowAccessibilityFocused(this);
    }
    if (action_data.action == ax::mojom::Action::kDoDefault) {
      return owner_->OnRowAccessibilityActivated(this);
    }
  }
  return views::View::HandleAccessibleAction(action_data);
}

bool SidebarTreeRowView::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  if (sender != editor_ || key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    owner_->CommitRename(node_id_, std::u16string(editor_->GetText()));
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    owner_->CancelRename(node_id_);
    return true;
  }
  return false;
}

gfx::Rect SidebarTreeRowView::DisclosureBounds() const {
  const int x =
      kLeadingPadding + base::saturated_cast<int>(depth_) * kIndentWidth;
  return gfx::Rect(
      x, 0, is_folder() && split_segment_count_ == 1 ? kDisclosureWidth : 0,
      height());
}

gfx::Rect SidebarTreeRowView::IconBounds() const {
  const int x =
      is_folder()
          ? DisclosureBounds().right() + 2
          : kLeadingPadding + base::saturated_cast<int>(depth_) * kIndentWidth;
  return gfx::Rect(x, std::max(0, (height() - kIconSize) / 2), kIconSize,
                   kIconSize);
}

gfx::Rect SidebarTreeRowView::TitleBounds() const {
  constexpr int kTitleTrailingGap = 7;
  const int x = IconBounds().right() + kIconTitleSpacing;
  int title_end = media_indicator_.IsEmpty() ? TrailingActionBounds().x()
                                             : MediaIndicatorBounds().x();
  title_end -= kTitleTrailingGap;
  // A split-drop preview labels the destination in the logical leading half.
  // The Label already tail-elides, so the trailing half stays visually clear
  // for the incoming pane preview in both LTR and RTL layouts.
  if (split_drop_target_) {
    title_end = std::min(title_end, width() / 2 - kTitleTrailingGap);
  }
  return gfx::Rect(x, 0, std::max(0, title_end - x), height());
}

gfx::Rect SidebarTreeRowView::MediaIndicatorBounds() const {
  const gfx::Rect action = TrailingActionBounds();
  return gfx::Rect(std::max(0, action.x() - kTrailingActionSize - 2),
                   action.y(), kTrailingActionSize, action.height());
}

gfx::Rect SidebarTreeRowView::TrailingActionBounds() const {
  return gfx::Rect(std::max(0, width() - kTrailingActionSize - 4),
                   std::max(0, (height() - kTrailingActionSize) / 2),
                   kTrailingActionSize,
                   std::min(kTrailingActionSize, height()));
}

bool SidebarTreeRowView::ShouldShowTrailingAction() const {
  // Selection/focus describes browser state, not pointer intent. Keep both the
  // loaded-tab power action and the unloaded saved-page remove action hidden
  // until the pointer is genuinely over this row, matching temporary tabs and
  // preventing a persistent close affordance from crowding the active title.
  return !split_drop_target_ && !is_folder() && hovered_;
}

bool SidebarTreeRowView::ShouldPaintTrailingState() const {
  return !split_drop_target_;
}

bool SidebarTreeRowView::IsTrailingActionAt(const gfx::Point& point) const {
  return ShouldShowTrailingAction() &&
         GetMirroredRect(TrailingActionBounds()).Contains(point);
}

void SidebarTreeRowView::UpdateAccessibility() {
  auto& accessibility = GetViewAccessibility();
  accessibility.SetIsInvisible(false);
  std::u16string accessible_name =
      split_drop_target_ ? split_with_prefix_ + u" " + title_ : title_;
  if (sleeping_) {
    accessible_name += u" — ";
    accessible_name += l10n_util::GetStringUTF16(IDS_AHOI_TAB_SLEEPING_TOOLTIP);
  }
  if (!status_text_.empty()) {
    accessible_name += u" — ";
    accessible_name += status_text_;
  }
  accessibility.SetName(accessible_name);
  std::u16string tooltip;
  if (sleeping_) {
    tooltip = l10n_util::GetStringUTF16(IDS_AHOI_TAB_SLEEPING_TOOLTIP);
  }
  if (!status_text_.empty()) {
    if (!tooltip.empty()) {
      tooltip += u" — ";
    }
    tooltip += status_text_;
  }
  SetTooltipText(tooltip);
  accessibility.SetHierarchicalLevel(base::saturated_cast<int>(depth_ + 1));
  accessibility.SetPosInSet(base::saturated_cast<int>(position_in_parent_));
  accessibility.SetSetSize(base::saturated_cast<int>(sibling_count_));
  accessibility.SetIsSelected(selected_);
  accessibility.SetDefaultActionVerb(is_folder()
                                         ? ax::mojom::DefaultActionVerb::kClick
                                         : ax::mojom::DefaultActionVerb::kOpen);
  accessibility.AddAction(ax::mojom::Action::kDoDefault);
  if (is_folder()) {
    if (expanded_) {
      accessibility.SetIsExpanded();
    } else {
      accessibility.SetIsCollapsed();
    }
  } else {
    accessibility.RemoveExpandCollapseState();
  }
}

void SidebarTreeRowView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation == &chevron_animation_) {
    SchedulePaint();
  }
}

void SidebarTreeRowView::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &chevron_animation_) {
    SchedulePaint();
  }
}

void SidebarTreeRowView::AnimationCanceled(const gfx::Animation* animation) {
  AnimationEnded(animation);
}

BEGIN_METADATA(SidebarTreeRowView)
END_METADATA

}  // namespace ahoi::sidebar
