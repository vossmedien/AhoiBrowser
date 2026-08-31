// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_media_indicator.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_support.h"
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_title_label.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

class OpenTabRowView final : public views::View, public views::DragController {
  METADATA_HEADER(OpenTabRowView, views::View)

 public:
  using TabCallback =
      base::RepeatingCallback<void(base::WeakPtr<tabs::TabInterface>)>;
  using DragStateCallback = base::RepeatingCallback<void(std::optional<int>)>;
  using CanDropCallback =
      base::RepeatingCallback<bool(std::optional<base::Uuid>,
                                   std::optional<int>,
                                   base::WeakPtr<tabs::TabInterface>,
                                   OpenTabDropPosition)>;
  using DropCallback =
      base::RepeatingCallback<bool(std::optional<base::Uuid>,
                                   std::optional<int>,
                                   base::WeakPtr<tabs::TabInterface>,
                                   OpenTabDropPosition)>;

  OpenTabRowView(tabs::TabInterface* tab,
                 std::optional<base::Uuid> saved_node_id,
                 ui::ImageModel favicon,
                 std::optional<tabs::TabAlert> media_alert,
                 std::u16string status_text,
                 bool active,
                 bool sleeping,
                 bool drag_enabled,
                 TabCallback activate_callback,
                 TabCallback close_callback,
                 RuntimeTabThumbnailsCallback thumbnails_callback,
                 RuntimeTabHoverCallback hover_callback,
                 SavedTabDragStateCallback saved_drag_state_callback,
                 DragStateCallback drag_state_callback,
                 SidebarDropTargetClaimCallback drop_target_claim_callback,
                 CanDropCallback can_drop_callback,
                 DropCallback drop_callback,
                 views::ContextMenuController* context_menu_controller)
      : tab_(tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()),
        runtime_tab_handle_(tab ? tab->GetHandle().raw_value() : -1),
        drag_title_(internal::StableTabTitle(tab)),
        saved_node_id_(std::move(saved_node_id)),
        activate_callback_(std::move(activate_callback)),
        close_callback_(std::move(close_callback)),
        thumbnails_callback_(std::move(thumbnails_callback)),
        hover_callback_(std::move(hover_callback)),
        saved_drag_state_callback_(std::move(saved_drag_state_callback)),
        drag_state_callback_(std::move(drag_state_callback)),
        drop_target_claim_callback_(std::move(drop_target_claim_callback)),
        can_drop_callback_(std::move(can_drop_callback)),
        drop_callback_(std::move(drop_callback)),
        drag_enabled_(drag_enabled),
        active_(active),
        sleeping_(sleeping) {
    CHECK(tab);
    CHECK(!saved_node_id_.has_value() || saved_node_id_->is_valid());
    const std::u16string& tab_title = drag_title_;
    SetPreferredSize(gfx::Size(0, SidebarTreeRowView::kRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetNotifyEnterExitOnChild(true);
    set_drag_controller(this);
    set_context_menu_controller(context_menu_controller);

    favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_view_->SetImage(std::move(favicon));
    favicon_view_->SetImageSize(gfx::Size(16, 16));
    favicon_view_->SetCanProcessEventsWithinSubtree(false);
    favicon_view_->GetViewAccessibility().SetIsIgnored(true);

    fallback_icon_ = AddChildView(
        CreatePageFallbackIconView(active_, internal::IsNewTabPage(tab)));
    fallback_icon_->SetVisible(favicon_view_->GetImageModel().IsEmpty());
    fallback_icon_->SetCanProcessEventsWithinSubtree(false);

    title_ = AddChildView(std::make_unique<SidebarTabTitleLabel>());
    title_->SetText(tab_title);
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetEnabledColor(visual_style::kText);

    media_indicator_ = AddChildView(std::make_unique<views::ImageView>());
    media_indicator_->SetImage(GetSidebarMediaIndicator(media_alert));
    media_indicator_->SetImageSize(gfx::Size(16, 16));
    media_indicator_->SetCanProcessEventsWithinSubtree(false);
    media_indicator_->GetViewAccessibility().SetIsIgnored(true);
    has_media_indicator_ = !media_indicator_->GetImageModel().IsEmpty();
    media_indicator_->SetVisible(has_media_indicator_);

    close_ = AddChildView(CreateTabCloseIconView(active_));
    close_->SetCanProcessEventsWithinSubtree(false);
    close_->SetVisible(false);

    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
    std::u16string accessible_name = tab_title;
    std::u16string tooltip;
    if (sleeping_) {
      const std::u16string sleeping_text =
          l10n_util::GetStringUTF16(IDS_AHOI_TAB_SLEEPING_TOOLTIP);
      accessible_name += u" — ";
      accessible_name += sleeping_text;
      tooltip = sleeping_text;
    }
    if (!status_text.empty()) {
      accessible_name += u" — ";
      accessible_name += status_text;
      if (!tooltip.empty()) {
        tooltip += u" — ";
      }
      tooltip += status_text;
    }
    SetTooltipText(tooltip);
    GetViewAccessibility().SetName(accessible_name);
    GetViewAccessibility().SetIsSelected(active_);
    UpdateBackground();
  }

  OpenTabRowView(const OpenTabRowView&) = delete;
  OpenTabRowView& operator=(const OpenTabRowView&) = delete;
  ~OpenTabRowView() override {
    if (hovered_) {
      hover_callback_.Run(tab_, this, false);
    }
    set_drag_controller(nullptr);
    set_context_menu_controller(nullptr);
  }

  base::WeakPtr<tabs::TabInterface> tab() const { return tab_; }
  const std::optional<base::Uuid>& saved_node_id() const {
    return saved_node_id_;
  }

  void SetSearchSelected(bool selected) {
    if (search_selected_ == selected) {
      return;
    }
    search_selected_ = selected;
    GetViewAccessibility().SetIsSelected(active_ || search_selected_);
    UpdateBackground();
  }

  void SetSplitSegmentPresentation(bool split_segment) {
    if (is_split_segment_ == split_segment) {
      return;
    }
    is_split_segment_ = split_segment;
    UpdateBackground();
    UpdateTitleBounds();
    InvalidateLayout();
  }

  void Layout(PassKey) override { UpdateTitleBounds(); }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!is_split_segment_) {
      views::View::OnPaintBackground(canvas);
      return;
    }
    const std::optional<ui::ColorId> color = SurfaceColor();
    if (!color.has_value()) {
      return;
    }
    gfx::RectF surface(GetLocalBounds());
    if (surface.IsEmpty()) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(false);
    fill.setColor(GetColorProvider()->GetColor(*color));
    fill.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRect(surface, fill);
  }

  void UpdateTitleBounds() {
    const gfx::Rect icon_bounds(8, std::max(0, (height() - 16) / 2), 16, 16);
    favicon_view_->SetBoundsRect(icon_bounds);
    fallback_icon_->SetBoundsRect(icon_bounds);
    const SidebarTabTrailingLayout trailing =
        GetSidebarTabTrailingLayout(width(), height(), has_media_indicator_);
    close_->SetBoundsRect(trailing.hover_action);
    media_indicator_->SetBoundsRect(trailing.media_indicator);
    gfx::Rect title_pane_bounds = GetLocalBounds();
    const bool split_drop_preview =
        drop_position_ == OpenTabDropPosition::kSplit;
    if (split_drop_preview) {
      // The existing tab stays in the logical leading half. Mirror that half
      // for RTL instead of always clipping the title to physical left.
      title_pane_bounds =
          GetMirroredRect(gfx::Rect(0, 0, std::max(0, width() / 2), height()));
    }
    title_->SetDividerSafeBounds(trailing.title, title_pane_bounds,
                                 is_split_segment_ || split_drop_preview);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    drag_started_since_press_ = false;
    close_pressed_ = CloseBounds().Contains(event.location());
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!event.IsLeftMouseButton()) {
      close_pressed_ = false;
      return;
    }
    const bool close =
        close_pressed_ && CloseBounds().Contains(event.location());
    close_pressed_ = false;
    if (std::exchange(drag_started_since_press_, false) ||
        !GetLocalBounds().Contains(event.location())) {
      return;
    }
    PostCallback(close ? close_callback_ : activate_callback_);
  }

  void OnMouseEntered(const ui::MouseEvent&) override {
    hovered_ = true;
    close_->SetVisible(true);
    UpdateBackground();
    hover_callback_.Run(tab_, this, true);
  }

  void OnMouseExited(const ui::MouseEvent&) override {
    hovered_ = false;
    close_pressed_ = false;
    close_->SetVisible(false);
    UpdateBackground();
    hover_callback_.Run(tab_, this, false);
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_RETURN) {
      PostCallback(activate_callback_);
      return true;
    }
    if (event.key_code() == ui::VKEY_BACK ||
        event.key_code() == ui::VKEY_DELETE) {
      PostCallback(close_callback_);
      return true;
    }
    return false;
  }

  void OnDragDone() override {
    dragging_ = false;
    drag_state_published_ = false;
    ClearDropPosition();
    ClearDragState();
    UpdateBackground();
    views::View::OnDragDone();
  }

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override {
    CHECK_EQ(sender, this);
    const gfx::ImageSkia image = GetDragImage();
    CHECK(!image.isNull());
    CHECK(!image.size().IsEmpty());
    data->provider().SetDragImage(
        image, GetSidebarDragImageCursorOffset(image, press_pt));
    // Give AppKit a concrete pasteboard item in addition to Ahoi's private
    // runtime-tab handle. Custom-only payloads can otherwise fail before the
    // native dragging session (and therefore before any preview) begins.
    WriteOpenTabDragPayload(data, saved_node_id_, runtime_tab_handle_,
                            drag_title_);
    // WriteDragData is the last deterministic boundary before Cocoa enters
    // its nested native loop. Publish here as well as in both lifecycle hooks
    // so the saved/new-group targets cannot depend on callback ordering.
    PublishDragState();
    drag_started_since_press_ = true;
    dragging_ = true;
    UpdateBackground();
  }

  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& point) override {
    return ui::DragDropTypes::DRAG_MOVE;
  }

  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point&) override {
    const bool allowed = drag_enabled_ && sender == this && tab_ &&
                         !CloseBounds().Contains(press_pt);
    return allowed;
  }

  void OnWillStartDragForView(views::View* dragged_view) override {
    if (dragged_view == this) {
      PublishDragState();
    }
  }

  void OnNativeDragStartedForView(views::View* dragged_view) override {
    if (dragged_view == this) {
      PublishDragState();
    }
  }

  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override {
    *formats |= ui::OSExchangeData::PICKLED_DATA;
    format_types->insert(drag::SavedSidebarTabDragFormat());
    format_types->insert(drag::RuntimeSidebarTabDragFormat());
    return true;
  }

  bool AreDropTypesRequired() override { return true; }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return drag::ReadSidebarTabDragPayload(data).has_value() && tab_;
  }

  void OnDragEntered(const ui::DropTargetEvent& event) override {
    UpdateDropPosition(event);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    return UpdateDropPosition(event) ? ui::DragDropTypes::DRAG_MOVE
                                     : ui::DragDropTypes::DRAG_NONE;
  }

  void OnDragExited() override { ClearDropPosition(); }

  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(event.data());
    if (!payload.has_value()) {
      if (drop_target_claim_callback_) {
        drop_target_claim_callback_.Run(nullptr);
      }
      ClearDropPosition();
      return {};
    }
    std::optional<OpenTabDropPosition> position = drop_position_;
    if (!position.has_value() ||
        !can_drop_callback_.Run(payload->saved_node_id,
                                payload->runtime_tab_handle, tab_, *position)) {
      position = AllowedPosition(*payload, event.location());
    }
    if (!position.has_value()) {
      if (drop_target_claim_callback_) {
        drop_target_claim_callback_.Run(nullptr);
      }
      ClearDropPosition();
      return {};
    }
    return base::BindOnce(
        &OpenTabRowView::PerformDrop, weak_ptr_factory_.GetWeakPtr(),
        payload->saved_node_id, payload->runtime_tab_handle, *position);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    if (sleeping_ && !hovered_) {
      const gfx::Rect status_bounds = CloseBounds();
      const gfx::Point center = status_bounds.CenterPoint();
      cc::PaintFlags sleep_stroke;
      sleep_stroke.setAntiAlias(true);
      sleep_stroke.setColor(
          GetColorProvider()->GetColor(visual_style::kAccent));
      sleep_stroke.setStrokeWidth(1.4f);
      sleep_stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
      sleep_stroke.setStrokeJoin(cc::PaintFlags::kRound_Join);
      sleep_stroke.setStyle(cc::PaintFlags::kStroke_Style);
      SkPathBuilder moon;
      moon.moveTo(center.x() + 3.5f, center.y() - 5.5f);
      moon.cubicTo(center.x() - 3.5f, center.y() - 5.5f, center.x() - 4.5f,
                   center.y() + 4.5f, center.x() + 3.5f, center.y() + 5.5f);
      moon.cubicTo(center.x() - 0.5f, center.y() + 3.5f, center.x() - 0.5f,
                   center.y() - 3.5f, center.x() + 3.5f, center.y() - 5.5f);
      canvas->DrawPath(moon.detach(), sleep_stroke);
    }
    if (!drop_position_.has_value()) {
      return;
    }
    cc::PaintFlags indicator;
    indicator.setAntiAlias(true);
    indicator.setColor(GetColorProvider()->GetColor(visual_style::kAccent));
    if (*drop_position_ == OpenTabDropPosition::kSplit) {
      gfx::RectF bounds(GetLocalBounds());
      bounds.Inset(gfx::InsetsF(1.0f));
      cc::PaintFlags fill = indicator;
      fill.setStyle(cc::PaintFlags::kFill_Style);
      fill.setColor(
          GetColorProvider()->GetColor(visual_style::kDropTargetSurface));
      canvas->DrawRoundRect(bounds, visual_style::kRowCornerRadius, fill);
      indicator.setStyle(cc::PaintFlags::kStroke_Style);
      indicator.setStrokeWidth(1.5f);
      canvas->DrawRoundRect(bounds, visual_style::kRowCornerRadius, indicator);
      indicator.setStrokeWidth(1.0f);
      const float divider_x = bounds.x() + bounds.width() * 0.5f;
      canvas->DrawLine(gfx::PointF(divider_x, bounds.y() + 5.0f),
                       gfx::PointF(divider_x, bounds.bottom() - 5.0f),
                       indicator);
      return;
    }
    const gfx::RectF zone = GetSidebarEdgeDropTargetBounds(
        GetLocalBounds(), *drop_position_ == OpenTabDropPosition::kAfter);
    cc::PaintFlags zone_fill = indicator;
    zone_fill.setStyle(cc::PaintFlags::kFill_Style);
    zone_fill.setColor(
        GetColorProvider()->GetColor(visual_style::kDropTargetSurface));
    canvas->DrawRoundRect(zone, visual_style::kRowCornerRadius, zone_fill);
    gfx::RectF outline = zone;
    constexpr float kDropOutlineWidth = static_cast<float>(
        visual_style::kSidebarDropTargetAcceptingOutlineThickness);
    outline.Inset(kDropOutlineWidth / 2.0f);
    indicator.setStyle(cc::PaintFlags::kStroke_Style);
    indicator.setStrokeWidth(kDropOutlineWidth);
    canvas->DrawRoundRect(outline,
                          std::max(0.0f, visual_style::kRowCornerRadius -
                                             kDropOutlineWidth / 2.0f),
                          indicator);

    // Keep a fixed semantic insertion edge inside the generous target
    // surface. It is derived from the row bounds and changes only when the
    // validated before/after zone changes, never with raw pointer movement.
    constexpr float kInsertionEdgeHeight = 3.0f;
    gfx::RectF insertion_edge(
        zone.x() + 4.0f,
        *drop_position_ == OpenTabDropPosition::kBefore
            ? zone.y()
            : std::max(zone.y(), zone.bottom() - kInsertionEdgeHeight),
        std::max(0.0f, zone.width() - 8.0f), kInsertionEdgeHeight);
    indicator.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(insertion_edge, kInsertionEdgeHeight / 2.0f,
                          indicator);
  }

  void ClearDropTargetPresentation() { ClearDropPosition(); }

 private:
  void ClearDropPosition() {
    if (!drop_position_.has_value()) {
      return;
    }
    drop_position_.reset();
    // Keep title geometry in lockstep with the painted split state even while
    // AppKit owns the nested native drag loop and ordinary Views layout is
    // deferred.
    UpdateTitleBounds();
    InvalidateLayout();
    SchedulePaint();
  }

  void PublishDragState() {
    if (drag_state_published_) {
      return;
    }
    drag_state_published_ = true;
    if (tab_) {
      hover_callback_.Run(tab_, this, false);
    }
    if (saved_node_id_.has_value()) {
      saved_drag_state_callback_.Run(saved_node_id_);
    } else {
      drag_state_callback_.Run(runtime_tab_handle_);
    }
  }

  void ClearDragState() {
    if (saved_node_id_.has_value()) {
      saved_drag_state_callback_.Run(std::nullopt);
    } else {
      drag_state_callback_.Run(std::nullopt);
    }
  }

  OpenTabDropPosition PositionForPoint(const gfx::Point& point) const {
    // Keep native hit testing identical to the painted 30/40/30 zones. A
    // highlighted region must never promise a drop that the pointer cannot
    // actually commit.
    const int edge_zone = GetSidebarEdgeDropTargetExtent(height());
    if (point.y() < edge_zone) {
      return OpenTabDropPosition::kBefore;
    }
    if (point.y() >= height() - edge_zone) {
      return OpenTabDropPosition::kAfter;
    }
    return OpenTabDropPosition::kSplit;
  }

  bool UpdateDropPosition(const ui::DropTargetEvent& event) {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(event.data());
    std::optional<OpenTabDropPosition> next =
        payload.has_value() ? AllowedPosition(*payload, event.location())
                            : std::nullopt;
    if (payload.has_value()) {
      next = StabilizeDropPosition(*payload, std::move(next), event.location());
    }
    if (drop_target_claim_callback_) {
      drop_target_claim_callback_.Run(next.has_value() ? this : nullptr);
    }
    if (drop_position_ != next) {
      drop_position_ = next;
      UpdateTitleBounds();
      InvalidateLayout();
      SchedulePaint();
    }
    return next.has_value();
  }

  std::optional<OpenTabDropPosition> StabilizeDropPosition(
      const drag::SidebarTabDragPayload& payload,
      std::optional<OpenTabDropPosition> next,
      const gfx::Point& point) const {
    if (!drop_position_.has_value() || next == drop_position_ ||
        !can_drop_callback_.Run(payload.saved_node_id,
                                payload.runtime_tab_handle, tab_,
                                *drop_position_)) {
      return next;
    }

    const int edge_extent = GetSidebarEdgeDropTargetExtent(height());
    const int before_boundary = edge_extent;
    const int after_boundary = height() - edge_extent;
    const int center_boundary = height() / 2;
    constexpr int kDropZoneHysteresis = 4;
    const OpenTabDropPosition current = *drop_position_;
    if ((current == OpenTabDropPosition::kBefore &&
         next == OpenTabDropPosition::kSplit &&
         point.y() < before_boundary + kDropZoneHysteresis) ||
        (current == OpenTabDropPosition::kSplit &&
         next == OpenTabDropPosition::kBefore &&
         point.y() >= before_boundary - kDropZoneHysteresis) ||
        (current == OpenTabDropPosition::kAfter &&
         next == OpenTabDropPosition::kSplit &&
         point.y() >= after_boundary - kDropZoneHysteresis) ||
        (current == OpenTabDropPosition::kSplit &&
         next == OpenTabDropPosition::kAfter &&
         point.y() < after_boundary + kDropZoneHysteresis) ||
        (current == OpenTabDropPosition::kBefore &&
         next == OpenTabDropPosition::kAfter &&
         point.y() < center_boundary + kDropZoneHysteresis) ||
        (current == OpenTabDropPosition::kAfter &&
         next == OpenTabDropPosition::kBefore &&
         point.y() >= center_boundary - kDropZoneHysteresis)) {
      return current;
    }
    return next;
  }

  std::optional<OpenTabDropPosition> AllowedPosition(
      const drag::SidebarTabDragPayload& payload,
      const gfx::Point& point) const {
    const OpenTabDropPosition preferred = PositionForPoint(point);
    if (can_drop_callback_.Run(payload.saved_node_id,
                               payload.runtime_tab_handle, tab_, preferred)) {
      return preferred;
    }
    if (preferred != OpenTabDropPosition::kSplit) {
      return std::nullopt;
    }

    // A rejected split is still a useful reorder gesture. Resolve the central
    // pointer to its nearest valid edge so the visible tab row has no dead
    // middle region.
    const OpenTabDropPosition nearest = point.y() < height() / 2
                                            ? OpenTabDropPosition::kBefore
                                            : OpenTabDropPosition::kAfter;
    return can_drop_callback_.Run(payload.saved_node_id,
                                  payload.runtime_tab_handle, tab_, nearest)
               ? std::optional(nearest)
               : std::nullopt;
  }

  void PerformDrop(std::optional<base::Uuid> source_node,
                   std::optional<int> source_tab,
                   OpenTabDropPosition position,
                   const ui::DropTargetEvent&,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner>) {
    // A successful drop may synchronously rebuild this row. Capture every
    // value the callback needs before clearing local paint state, then invoke
    // the stable callback last and never touch members afterwards.
    const DropCallback drop_callback = drop_callback_;
    const base::WeakPtr<tabs::TabInterface> target = tab_;
    ClearDropPosition();
    output_drag_op = drop_callback && drop_callback.Run(source_node, source_tab,
                                                        target, position)
                         ? ui::mojom::DragOperation::kMove
                         : ui::mojom::DragOperation::kNone;
  }

  gfx::ImageSkia GetDragImage() {
    const ui::ColorProvider* const colors = GetColorProvider();
    const ui::ImageModel& favicon_model = favicon_view_->GetImageModel();
    gfx::ImageSkia favicon;
    if (!favicon_model.IsEmpty() && (colors || favicon_model.IsImage())) {
      favicon = favicon_model.Rasterize(colors);
    }
    const std::vector<gfx::ImageSkia> thumbnails =
        tab_ ? thumbnails_callback_.Run(tab_) : std::vector<gfx::ImageSkia>();
    return CreateSidebarDragImage(GetWidget(), colors, favicon, drag_title_,
                                  thumbnails);
  }

  gfx::Rect CloseBounds() const {
    return GetSidebarTabTrailingLayout(width(), height(), has_media_indicator_)
        .hover_action;
  }

  void PostCallback(const TabCallback& callback) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(callback, tab_));
  }

  void UpdateBackground() {
    const std::optional<ui::ColorId> color = SurfaceColor();
    SetBackground(
        color.has_value() && !is_split_segment_
            ? views::CreateRoundedRectBackground(
                  *color, gfx::RoundedCornersF(visual_style::kRowCornerRadius),
                  gfx::Insets::VH(visual_style::kSidebarTabRowVerticalInset,
                                  visual_style::kSidebarTabRowHorizontalInset))
            : nullptr);
    SchedulePaint();
    if (is_split_segment_ && parent()) {
      parent()->SchedulePaint();
    }
  }

  std::optional<ui::ColorId> SurfaceColor() const {
    return dragging_ ? std::nullopt
           : active_ || search_selected_
               ? std::make_optional(visual_style::kSelectedSurface)
           : hovered_ ? std::make_optional(visual_style::kHoverSurface)
                      : std::nullopt;
  }

  const base::WeakPtr<tabs::TabInterface> tab_;
  const int runtime_tab_handle_;
  const std::u16string drag_title_;
  const std::optional<base::Uuid> saved_node_id_;
  const TabCallback activate_callback_;
  const TabCallback close_callback_;
  const RuntimeTabThumbnailsCallback thumbnails_callback_;
  const RuntimeTabHoverCallback hover_callback_;
  const SavedTabDragStateCallback saved_drag_state_callback_;
  const DragStateCallback drag_state_callback_;
  const SidebarDropTargetClaimCallback drop_target_claim_callback_;
  const CanDropCallback can_drop_callback_;
  const DropCallback drop_callback_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::View> fallback_icon_ = nullptr;
  raw_ptr<SidebarTabTitleLabel> title_ = nullptr;
  raw_ptr<views::ImageView> media_indicator_ = nullptr;
  raw_ptr<views::View> close_ = nullptr;
  const bool drag_enabled_;
  const bool active_;
  const bool sleeping_;
  bool has_media_indicator_ = false;
  bool is_split_segment_ = false;
  bool hovered_ = false;
  bool close_pressed_ = false;
  bool dragging_ = false;
  bool drag_started_since_press_ = false;
  bool drag_state_published_ = false;
  bool search_selected_ = false;
  std::optional<OpenTabDropPosition> drop_position_;
  base::WeakPtrFactory<OpenTabRowView> weak_ptr_factory_{this};
};

BEGIN_METADATA(OpenTabRowView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateOpenTabRowView(
    tabs::TabInterface* tab,
    std::optional<base::Uuid> saved_node_id,
    ui::ImageModel favicon,
    std::optional<tabs::TabAlert> media_alert,
    std::u16string status_text,
    bool active,
    bool sleeping,
    bool drag_enabled,
    RuntimeTabCallback activate_callback,
    RuntimeTabCallback close_callback,
    RuntimeTabThumbnailsCallback thumbnails_callback,
    RuntimeTabHoverCallback hover_callback,
    SavedTabDragStateCallback saved_drag_state_callback,
    RuntimeTabDragStateCallback drag_state_callback,
    SidebarDropTargetClaimCallback drop_target_claim_callback,
    CanDropOnRuntimeTabCallback can_drop_callback,
    DropOnRuntimeTabCallback drop_callback,
    views::ContextMenuController* context_menu_controller) {
  return std::make_unique<OpenTabRowView>(
      tab, std::move(saved_node_id), std::move(favicon), media_alert,
      std::move(status_text), active, sleeping, drag_enabled,
      std::move(activate_callback), std::move(close_callback),
      std::move(thumbnails_callback), std::move(hover_callback),
      std::move(saved_drag_state_callback), std::move(drag_state_callback),
      std::move(drop_target_claim_callback), std::move(can_drop_callback),
      std::move(drop_callback), context_menu_controller);
}

base::WeakPtr<tabs::TabInterface> GetOpenTabForView(views::View* view) {
  auto* row = views::AsViewClass<OpenTabRowView>(view);
  return row ? row->tab() : base::WeakPtr<tabs::TabInterface>();
}

std::optional<base::Uuid> GetSavedNodeForOpenTabView(views::View* view) {
  auto* row = views::AsViewClass<OpenTabRowView>(view);
  return row ? row->saved_node_id() : std::nullopt;
}

void SetOpenTabSearchSelected(views::View* view, bool selected) {
  if (auto* row = views::AsViewClass<OpenTabRowView>(view)) {
    row->SetSearchSelected(selected);
  }
}

bool SetOpenTabSplitSegmentPresentation(views::View* view) {
  auto* const row = views::AsViewClass<OpenTabRowView>(view);
  if (!row) {
    return false;
  }
  row->SetSplitSegmentPresentation(true);
  return true;
}

void internal::ClearOpenTabRowDropTargetPresentationForView(views::View* view) {
  if (auto* const row = views::AsViewClass<OpenTabRowView>(view)) {
    row->ClearDropTargetPresentation();
  }
}

}  // namespace ahoi::sidebar
