// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include <utility>

#include "ahoi/browser/ui/drag/sidebar_tab_drag_payload.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_row_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ahoi::sidebar {

namespace {

class OpenTabsDropTargetView final : public views::View {
  METADATA_HEADER(OpenTabsDropTargetView, views::View)

 public:
  using DropNodeCallback = base::RepeatingCallback<bool(const base::Uuid&)>;

  explicit OpenTabsDropTargetView(DropNodeCallback callback)
      : callback_(std::move(callback)) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
  }

  OpenTabsDropTargetView(const OpenTabsDropTargetView&) = delete;
  OpenTabsDropTargetView& operator=(const OpenTabsDropTargetView&) = delete;
  ~OpenTabsDropTargetView() override = default;

  void SetAcceptingSavedTab(bool accepting) {
    if (accepting_saved_tab_ == accepting) {
      return;
    }
    accepting_saved_tab_ = accepting;
    if (!accepting) {
      highlighted_ = false;
    }
    UpdateDropTargetBackground();
    PreferredSizeChanged();
  }

  bool accepting_saved_tab_for_testing() const { return accepting_saved_tab_; }

  bool highlighted_for_testing() const { return highlighted_; }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size preferred = views::View::CalculatePreferredSize(available_size);
    if (accepting_saved_tab_) {
      preferred.set_height(
          std::max(preferred.height(), SidebarTreeRowView::kRowHeight));
    }
    return preferred;
  }

  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override {
    *formats |= ui::OSExchangeData::PICKLED_DATA;
    format_types->insert(drag::SavedSidebarTabDragFormat());
    return true;
  }

  bool AreDropTypesRequired() override { return true; }

  bool CanDrop(const ui::OSExchangeData& data) override {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(data);
    return accepting_saved_tab_ && payload.has_value() &&
           payload->is_saved_tab();
  }

  void OnDragEntered(const ui::DropTargetEvent&) override {
    SetHighlighted(true);
  }

  int OnDragUpdated(const ui::DropTargetEvent&) override {
    return accepting_saved_tab_ ? ui::DragDropTypes::DRAG_MOVE
                                : ui::DragDropTypes::DRAG_NONE;
  }

  void OnDragExited() override { SetHighlighted(false); }

  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(event.data());
    if (!payload.has_value() || !payload->saved_node_id.has_value()) {
      return {};
    }
    return base::BindOnce(&OpenTabsDropTargetView::PerformDrop,
                          weak_ptr_factory_.GetWeakPtr(),
                          *payload->saved_node_id);
  }

 private:
  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted && accepting_saved_tab_;
    UpdateDropTargetBackground();
  }

  void UpdateDropTargetBackground() {
    SetBackground(
        accepting_saved_tab_
            ? views::CreateRoundedRectBackground(
                  highlighted_ ? visual_style::kDropTargetSurface
                               : visual_style::kHoverSurface,
                  gfx::RoundedCornersF(visual_style::kRowCornerRadius),
                  gfx::Insets(visual_style::kSidebarDropTargetInset))
            : nullptr);
    SchedulePaint();
  }

  void PerformDrop(base::Uuid source_node_id,
                   const ui::DropTargetEvent&,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner>) {
    SetHighlighted(false);
    output_drag_op = callback_.Run(source_node_id)
                         ? ui::mojom::DragOperation::kMove
                         : ui::mojom::DragOperation::kNone;
  }

  const DropNodeCallback callback_;
  bool accepting_saved_tab_ = false;
  bool highlighted_ = false;
  base::WeakPtrFactory<OpenTabsDropTargetView> weak_ptr_factory_{this};
};

BEGIN_METADATA(OpenTabsDropTargetView)
END_METADATA

class NewGroupDropTargetView final : public views::View,
                                     public gfx::AnimationDelegate {
  METADATA_HEADER(NewGroupDropTargetView, views::View)

 public:
  using DropNodeCallback = base::RepeatingCallback<void(const base::Uuid&)>;
  using DropRuntimeTabCallback = base::RepeatingCallback<void(int)>;

  NewGroupDropTargetView(DropNodeCallback node_callback,
                         DropRuntimeTabCallback runtime_tab_callback,
                         std::u16string label_text,
                         std::u16string accessible_name)
      : node_callback_(std::move(node_callback)),
        runtime_tab_callback_(std::move(runtime_tab_callback)) {
    SetPreferredSize(gfx::Size(0, visual_style::kSidebarActionCellHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 13), 9));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon = AddChildView(
        std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kAddWeight500Icon, visual_style::kAccent, 18)));
    icon->SetPreferredSize(gfx::Size(20, 20));
    icon->SetCanProcessEventsWithinSubtree(false);
    icon->GetViewAccessibility().SetIsIgnored(true);

    if (label_text.starts_with(u"+ ")) {
      label_text.erase(0, 2);
    }
    auto* label = AddChildView(std::make_unique<views::Label>(label_text));
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetEnabledColor(visual_style::kText);
    label->SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(accessible_name);
    SetPaintToLayer();
    // The rounded background leaves transparent corner pixels. Marking this
    // composited fade layer as opaque makes CoreAnimation substitute bright
    // edge/corner pixels while its height changes.
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    visibility_animation_.SetSlideDuration(visual_style::kTreeMotionDuration);
    visibility_animation_.Reset(0.0);
    SetVisible(false);
    SetCanProcessEventsWithinSubtree(false);
    SetHighlighted(false);
  }

  NewGroupDropTargetView(const NewGroupDropTargetView&) = delete;
  NewGroupDropTargetView& operator=(const NewGroupDropTargetView&) = delete;
  ~NewGroupDropTargetView() override = default;

  void SetDropTargetVisible(bool visible) {
    if (target_visible_ == visible) {
      return;
    }
    target_visible_ = visible;
    if (visible) {
      // This normally hidden content row must participate in BoxLayout before
      // AppKit enters its nested native drag loop. Resolve its own stable row
      // bounds synchronously; never borrow the workspace header's bounds.
      SetCanProcessEventsWithinSubtree(true);
      SetVisible(true);
      PreferredSizeChanged();
      if (parent()) {
        parent()->InvalidateLayout();
        parent()->DeprecatedLayoutImmediately();
      }
      visibility_animation_.Show();
    } else if (GetVisible()) {
      SetCanProcessEventsWithinSubtree(false);
      SetHighlighted(false);
      visibility_animation_.Hide();
    }
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(0, visual_style::kSidebarActionCellHeight);
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
    return drag::ReadSidebarTabDragPayload(data).has_value();
  }

  void OnDragEntered(const ui::DropTargetEvent&) override {
    SetHighlighted(true);
  }

  int OnDragUpdated(const ui::DropTargetEvent&) override {
    return ui::DragDropTypes::DRAG_MOVE;
  }

  void OnDragExited() override { SetHighlighted(false); }

  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override {
    const std::optional<drag::SidebarTabDragPayload> payload =
        drag::ReadSidebarTabDragPayload(event.data());
    if (!payload.has_value()) {
      return {};
    }
    if (payload->saved_node_id.has_value()) {
      return base::BindOnce(&NewGroupDropTargetView::PerformNodeDrop,
                            weak_ptr_factory_.GetWeakPtr(),
                            *payload->saved_node_id);
    }
    return payload->runtime_tab_handle.has_value()
               ? base::BindOnce(&NewGroupDropTargetView::PerformRuntimeTabDrop,
                                weak_ptr_factory_.GetWeakPtr(),
                                *payload->runtime_tab_handle)
               : views::View::DropCallback();
  }

 private:
  void SetHighlighted(bool highlighted) {
    SetBackground(views::CreateRoundedRectBackground(
        highlighted ? visual_style::kDropTargetSurface
                    : visual_style::kRaisedSurface,
        visual_style::kControlCornerRadius));
    // Use fill only for both states. Even an inset border turns into detached
    // arcs while a rounded row animates through a very small height.
    SetBorder(nullptr);
    SchedulePaint();
  }

  void PerformNodeDrop(base::Uuid source_node_id,
                       const ui::DropTargetEvent&,
                       ui::mojom::DragOperation& output_drag_op,
                       std::unique_ptr<ui::LayerTreeOwner>) {
    SetHighlighted(false);
    node_callback_.Run(source_node_id);
    output_drag_op = ui::mojom::DragOperation::kMove;
  }

  void PerformRuntimeTabDrop(int runtime_tab_handle,
                             const ui::DropTargetEvent&,
                             ui::mojom::DragOperation& output_drag_op,
                             std::unique_ptr<ui::LayerTreeOwner>) {
    SetHighlighted(false);
    runtime_tab_callback_.Run(runtime_tab_handle);
    output_drag_op = ui::mojom::DragOperation::kMove;
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    if (animation != &visibility_animation_) {
      return;
    }
    layer()->SetOpacity(
        static_cast<float>(visibility_animation_.GetCurrentValue()));
    SchedulePaint();
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    if (animation != &visibility_animation_) {
      return;
    }
    if (!target_visible_) {
      SetVisible(false);
      PreferredSizeChanged();
      if (parent()) {
        parent()->InvalidateLayout();
      }
    }
    SchedulePaint();
  }

  void AnimationCanceled(const gfx::Animation* animation) override {
    AnimationEnded(animation);
  }

  const DropNodeCallback node_callback_;
  const DropRuntimeTabCallback runtime_tab_callback_;
  gfx::SlideAnimation visibility_animation_{this};
  bool target_visible_ = false;
  base::WeakPtrFactory<NewGroupDropTargetView> weak_ptr_factory_{this};
};

BEGIN_METADATA(NewGroupDropTargetView)
END_METADATA

}  // namespace

std::unique_ptr<views::View> CreateOpenTabsDropTargetView(
    DropSavedNodeToTemporaryCallback callback) {
  return std::make_unique<OpenTabsDropTargetView>(std::move(callback));
}

void SetOpenTabsDropTargetAcceptingSavedTab(views::View* view, bool accepting) {
  if (auto* target = views::AsViewClass<OpenTabsDropTargetView>(view)) {
    target->SetAcceptingSavedTab(accepting);
  }
}

bool IsOpenTabsDropTargetAcceptingSavedTabForTesting(const views::View* view) {
  const auto* target = views::AsViewClass<OpenTabsDropTargetView>(view);
  return target && target->accepting_saved_tab_for_testing();
}

bool IsOpenTabsDropTargetHighlightedForTesting(const views::View* view) {
  const auto* target = views::AsViewClass<OpenTabsDropTargetView>(view);
  return target && target->highlighted_for_testing();
}

std::unique_ptr<views::View> CreateNewGroupDropTargetView(
    CreateGroupForSavedNodeCallback node_callback,
    CreateGroupForRuntimeTabCallback runtime_tab_callback) {
  return std::make_unique<NewGroupDropTargetView>(
      std::move(node_callback), std::move(runtime_tab_callback),
      l10n_util::GetStringUTF16(IDS_AHOI_NEW_GROUP_DROP_TARGET),
      l10n_util::GetStringUTF16(IDS_AHOI_NEW_GROUP_CREATE_ACCESSIBLE_NAME));
}

void SetNewGroupDropTargetVisible(views::View* view, bool visible) {
  if (auto* target = views::AsViewClass<NewGroupDropTargetView>(view)) {
    target->SetDropTargetVisible(visible);
  }
}

std::unique_ptr<views::View> CreateNewGroupDropTargetViewForTesting(
    std::u16string label) {
  return std::make_unique<NewGroupDropTargetView>(
      base::BindRepeating([](const base::Uuid&) {}),
      base::BindRepeating([](int) {}), label, label);
}

}  // namespace ahoi::sidebar
