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
#include "ahoi/browser/ui/sidebar/sidebar_split_layout.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
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
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

bool CanDetachRuntimeSplitPaneOnSelfDrop(bool source_is_split,
                                         OpenTabDropPosition position) {
  return source_is_split && position != OpenTabDropPosition::kSplit;
}

void WriteOpenTabDragPayload(ui::OSExchangeData* data,
                             std::optional<base::Uuid> saved_node_id,
                             int runtime_tab_handle,
                             const std::u16string& fallback_title) {
  if (saved_node_id.has_value()) {
    drag::WriteSavedSidebarTabDragPayload(data, *saved_node_id, fallback_title);
    return;
  }
  drag::WriteRuntimeSidebarTabDragPayload(data, runtime_tab_handle,
                                          fallback_title);
}

ui::ImageModel GetLiveTabFavicon(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  favicon::ContentFaviconDriver* driver =
      contents ? favicon::ContentFaviconDriver::FromWebContents(contents)
               : nullptr;
  return driver ? ui::ImageModel::FromImage(driver->GetFavicon())
                : ui::ImageModel();
}

namespace {

bool IsNewTabPage(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return false;
  }
  GURL url = contents->GetVisibleURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetLastCommittedURL();
  }
  return url == GURL(chrome::kChromeUINewTabURL);
}

std::u16string StableTabTitle(tabs::TabInterface* tab) {
  return !tab || tab->GetTitle().empty()
             ? l10n_util::GetStringUTF16(IDS_NEW_TAB)
             : tab->GetTitle();
}

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
                 TabCallback activate_callback,
                 TabCallback close_callback,
                 RuntimeTabThumbnailsCallback thumbnails_callback,
                 RuntimeTabHoverCallback hover_callback,
                 SavedTabDragStateCallback saved_drag_state_callback,
                 DragStateCallback drag_state_callback,
                 CanDropCallback can_drop_callback,
                 DropCallback drop_callback,
                 views::ContextMenuController* context_menu_controller)
      : tab_(tab ? tab->GetWeakPtr() : base::WeakPtr<tabs::TabInterface>()),
        runtime_tab_handle_(tab ? tab->GetHandle().raw_value() : -1),
        drag_title_(StableTabTitle(tab)),
        saved_node_id_(std::move(saved_node_id)),
        activate_callback_(std::move(activate_callback)),
        close_callback_(std::move(close_callback)),
        thumbnails_callback_(std::move(thumbnails_callback)),
        hover_callback_(std::move(hover_callback)),
        saved_drag_state_callback_(std::move(saved_drag_state_callback)),
        drag_state_callback_(std::move(drag_state_callback)),
        can_drop_callback_(std::move(can_drop_callback)),
        drop_callback_(std::move(drop_callback)),
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

    fallback_icon_ =
        AddChildView(CreatePageFallbackIconView(active_, IsNewTabPage(tab)));
    fallback_icon_->SetVisible(favicon_view_->GetImageModel().IsEmpty());
    fallback_icon_->SetCanProcessEventsWithinSubtree(false);

    title_ = AddChildView(std::make_unique<views::Label>(tab_title));
    title_->SetSubpixelRenderingEnabled(false);
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetEnabledColor(visual_style::kText);
    title_->SetCanProcessEventsWithinSubtree(false);
    title_->GetViewAccessibility().SetIsIgnored(true);

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

  void Layout(PassKey) override {
    const gfx::Rect icon_bounds(8, std::max(0, (height() - 16) / 2), 16, 16);
    favicon_view_->SetBoundsRect(icon_bounds);
    fallback_icon_->SetBoundsRect(icon_bounds);
    const SidebarTabTrailingLayout trailing =
        GetSidebarTabTrailingLayout(width(), height(), has_media_indicator_);
    close_->SetBoundsRect(trailing.hover_action);
    media_indicator_->SetBoundsRect(trailing.media_indicator);
    title_->SetBoundsRect(trailing.title);
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
    const bool allowed =
        sender == this && tab_ && !CloseBounds().Contains(press_pt);
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

  void OnDragExited() override {
    drop_position_.reset();
    SchedulePaint();
  }

  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(event.data());
    if (!payload.has_value()) {
      return {};
    }
    const std::optional<OpenTabDropPosition> position =
        AllowedPosition(*payload, event.location());
    if (!position.has_value()) {
      drop_position_.reset();
      SchedulePaint();
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
    const float zone_height = static_cast<float>(
        std::clamp(height() * 3 / 10, 1, std::max(1, (height() - 1) / 2)));
    gfx::RectF zone(
        visual_style::kSidebarTabRowHorizontalInset, 0.0f,
        std::max(0, width() - 2 * visual_style::kSidebarTabRowHorizontalInset),
        zone_height);
    if (*drop_position_ == OpenTabDropPosition::kAfter) {
      zone.set_y(std::max(0.0f, static_cast<float>(height()) - zone_height));
    }
    cc::PaintFlags zone_fill = indicator;
    zone_fill.setStyle(cc::PaintFlags::kFill_Style);
    zone_fill.setColor(
        GetColorProvider()->GetColor(visual_style::kDropTargetSurface));
    canvas->DrawRoundRect(zone, visual_style::kRowCornerRadius, zone_fill);

    indicator.setStyle(cc::PaintFlags::kFill_Style);
    constexpr float kInsertionIndicatorHeight = 3.0f;
    const float y = *drop_position_ == OpenTabDropPosition::kBefore
                        ? 0.0f
                        : std::max(0.0f, static_cast<float>(height()) -
                                             kInsertionIndicatorHeight);
    canvas->DrawRoundRect(gfx::RectF(6.0f, y, std::max(0, width() - 12),
                                     kInsertionIndicatorHeight),
                          kInsertionIndicatorHeight / 2.0f, indicator);
  }

 private:
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
    const int edge_zone =
        std::clamp(height() * 3 / 10, 1, std::max(1, (height() - 1) / 2));
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
    const std::optional<OpenTabDropPosition> next =
        payload.has_value() ? AllowedPosition(*payload, event.location())
                            : std::nullopt;
    if (drop_position_ != next) {
      drop_position_ = next;
      SchedulePaint();
    }
    return next.has_value();
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
    drop_position_.reset();
    SchedulePaint();
    output_drag_op = drop_callback_.Run(source_node, source_tab, tab_, position)
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
    const std::optional<ui::ColorId> color =
        dragging_  ? std::nullopt
        : active_  ? std::make_optional(visual_style::kSelectedSurface)
        : hovered_ ? std::make_optional(visual_style::kHoverSurface)
                   : std::nullopt;
    SetBackground(
        color.has_value()
            ? views::CreateRoundedRectBackground(
                  *color, gfx::RoundedCornersF(visual_style::kRowCornerRadius),
                  gfx::Insets::VH(visual_style::kSidebarTabRowVerticalInset,
                                  visual_style::kSidebarTabRowHorizontalInset))
            : nullptr);
    SchedulePaint();
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
  const CanDropCallback can_drop_callback_;
  const DropCallback drop_callback_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::View> fallback_icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> media_indicator_ = nullptr;
  raw_ptr<views::View> close_ = nullptr;
  const bool active_;
  const bool sleeping_;
  bool has_media_indicator_ = false;
  bool hovered_ = false;
  bool close_pressed_ = false;
  bool dragging_ = false;
  bool drag_started_since_press_ = false;
  bool drag_state_published_ = false;
  std::optional<OpenTabDropPosition> drop_position_;
  base::WeakPtrFactory<OpenTabRowView> weak_ptr_factory_{this};
};

BEGIN_METADATA(OpenTabRowView)
END_METADATA

// A live Chromium split is one visual row in the sidebar as well. Temporary
// panes and mixed saved/temporary collections live in this composite runtime
// representation, following SplitTabData rather than inferring membership
// from adjacency in TabStripModel.
class OpenTabSplitRowView final : public views::View {
  METADATA_HEADER(OpenTabSplitRowView, views::View)

 public:
  OpenTabSplitRowView(std::vector<std::unique_ptr<views::View>> tabs,
                      split_tabs::SplitTabVisualData visual_data)
      : visual_data_(std::move(visual_data)) {
    CHECK_GE(tabs.size(), 2u);
    SetPreferredSize(gfx::Size(
        0, GetSplitRowPreferredHeight(tabs.size(), visual_data_,
                                      SidebarTreeRowView::kRowHeight)));
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    for (auto& tab : tabs) {
      AddChildView(std::move(tab));
    }
  }

  OpenTabSplitRowView(const OpenTabSplitRowView&) = delete;
  OpenTabSplitRowView& operator=(const OpenTabSplitRowView&) = delete;
  ~OpenTabSplitRowView() override = default;

  void Layout(PassKey) override {
    const int count = static_cast<int>(children().size());
    if (count == 0) {
      return;
    }
    const gfx::Rect bounds = GetContentsBounds();
    for (int index = 0; index < count; ++index) {
      children()[index]->SetBoundsRect(
          GetSplitSegmentBounds(bounds, index, count, visual_data_));
    }
  }

 private:
  const split_tabs::SplitTabVisualData visual_data_;
};

BEGIN_METADATA(OpenTabSplitRowView)
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
    RuntimeTabCallback activate_callback,
    RuntimeTabCallback close_callback,
    RuntimeTabThumbnailsCallback thumbnails_callback,
    RuntimeTabHoverCallback hover_callback,
    SavedTabDragStateCallback saved_drag_state_callback,
    RuntimeTabDragStateCallback drag_state_callback,
    CanDropOnRuntimeTabCallback can_drop_callback,
    DropOnRuntimeTabCallback drop_callback,
    views::ContextMenuController* context_menu_controller) {
  return std::make_unique<OpenTabRowView>(
      tab, std::move(saved_node_id), std::move(favicon), media_alert,
      std::move(status_text), active, sleeping, std::move(activate_callback),
      std::move(close_callback), std::move(thumbnails_callback),
      std::move(hover_callback), std::move(saved_drag_state_callback),
      std::move(drag_state_callback), std::move(can_drop_callback),
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

std::unique_ptr<views::View> CreateOpenTabSplitRowView(
    std::vector<std::unique_ptr<views::View>> tabs,
    split_tabs::SplitTabVisualData visual_data) {
  return std::make_unique<OpenTabSplitRowView>(std::move(tabs),
                                               std::move(visual_data));
}

}  // namespace ahoi::sidebar
