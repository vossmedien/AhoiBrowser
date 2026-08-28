// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tab_preview_controller.h"

#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/time/time.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

constexpr base::TimeDelta kPreviewOpenDelay = base::Milliseconds(320);

}  // namespace

SidebarTabPreviewTarget SidebarTabPreviewTarget::SavedPage(base::Uuid node_id) {
  return {.kind = Kind::kSavedPage,
          .saved_node_id = std::move(node_id),
          .runtime_tab_handle = -1};
}

SidebarTabPreviewTarget SidebarTabPreviewTarget::RuntimeTab(int tab_handle) {
  return {.kind = Kind::kRuntimeTab,
          .saved_node_id = base::Uuid(),
          .runtime_tab_handle = tab_handle};
}

SidebarTabPreviewController::SidebarTabPreviewController(
    ResolveDataCallback resolve_data,
    ValidateAnchorCallback validate_anchor)
    : resolve_data_(std::move(resolve_data)),
      validate_anchor_(std::move(validate_anchor)) {}

SidebarTabPreviewController::~SidebarTabPreviewController() {
  show_timer_.Stop();
  weak_ptr_factory_.InvalidateWeakPtrs();
  preview_image_view_ = nullptr;
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

void SidebarTabPreviewController::OnSavedPageHover(const base::Uuid& node_id,
                                                   views::View* anchor,
                                                   bool hovered) {
  if (!node_id.is_valid()) {
    return;
  }
  Request(SidebarTabPreviewTarget::SavedPage(node_id), anchor, hovered);
}

void SidebarTabPreviewController::OnRuntimeTabHover(int tab_handle,
                                                    views::View* anchor,
                                                    bool hovered) {
  if (tab_handle < 0) {
    return;
  }
  Request(SidebarTabPreviewTarget::RuntimeTab(tab_handle), anchor, hovered);
}

void SidebarTabPreviewController::Refresh() {
  if (!target_.has_value()) {
    return;
  }
  if (!HasValidCurrentAnchor()) {
    Hide();
    return;
  }
  if (!bubble_widget_) {
    return;
  }
  std::optional<SidebarTabPreviewData> data = resolve_data_.Run(*target_);
  if (!data.has_value()) {
    Hide();
    return;
  }
  UpdateBubble(*data);
}

void SidebarTabPreviewController::Hide() {
  target_.reset();
  anchor_tracker_.SetView(nullptr);
  show_timer_.Stop();
  CloseBubble();
}

void SidebarTabPreviewController::Request(SidebarTabPreviewTarget target,
                                          views::View* anchor,
                                          bool hovered) {
  if (!hovered) {
    if (target_ == target) {
      Hide();
    }
    return;
  }
  if (!anchor || !validate_anchor_.Run(target, anchor)) {
    return;
  }

  const bool same_target = target_ == target;
  target_ = std::move(target);
  anchor_tracker_.SetView(anchor);
  show_timer_.Stop();
  if (bubble_widget_) {
    if (!same_target && bubble_delegate_) {
      bubble_delegate_->SetAnchorView(anchor);
    }
    Refresh();
    return;
  }
  show_timer_.Start(FROM_HERE, kPreviewOpenDelay,
                    base::BindOnce(&SidebarTabPreviewController::ShowAfterDelay,
                                   weak_ptr_factory_.GetWeakPtr()));
}

bool SidebarTabPreviewController::HasValidCurrentAnchor() const {
  return target_.has_value() && anchor_tracker_.view() &&
         validate_anchor_.Run(*target_, anchor_tracker_.view());
}

void SidebarTabPreviewController::ShowAfterDelay() {
  if (!HasValidCurrentAnchor()) {
    Hide();
    return;
  }
  std::optional<SidebarTabPreviewData> data = resolve_data_.Run(*target_);
  if (!data.has_value()) {
    Hide();
    return;
  }
  UpdateBubble(*data);
}

void SidebarTabPreviewController::UpdateBubble(
    const SidebarTabPreviewData& data) {
  views::View* const anchor = anchor_tracker_.view();
  if (!anchor || !target_.has_value()) {
    Hide();
    return;
  }
  const gfx::ImageSkia preview =
      CreateSidebarPreviewImage(anchor->GetWidget(), anchor->GetColorProvider(),
                                data.favicon, data.title, data.thumbnails);
  if (preview.isNull() || preview.size().IsEmpty()) {
    Hide();
    return;
  }

  if (bubble_widget_ && bubble_delegate_ && preview_image_view_) {
    bubble_delegate_->SetAnchorView(anchor);
    bubble_delegate_->SetAccessibleTitle(data.title);
    preview_image_view_->SetImage(ui::ImageModel::FromImageSkia(preview));
    preview_image_view_->SetImageSize(preview.size());
    preview_image_view_->SetPreferredSize(preview.size());
    preview_image_view_->InvalidateLayout();
    bubble_delegate_->SizeToContents();
    return;
  }

  auto image_view = std::make_unique<views::ImageView>();
  preview_image_view_ = image_view.get();
  preview_image_view_->SetImage(ui::ImageModel::FromImageSkia(preview));
  preview_image_view_->SetImageSize(preview.size());
  preview_image_view_->SetPreferredSize(preview.size());
  preview_image_view_->SetCanProcessEventsWithinSubtree(false);
  preview_image_view_->GetViewAccessibility().SetIsIgnored(true);

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::LEFT_CENTER,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetAccessibleTitle(data.title);
  delegate->SetBackgroundColor(visual_style::kChromeSurface);
  delegate->SetCanActivate(false);
  // The preview deliberately overlaps the renderer. It must remain completely
  // transparent to pointer and keyboard input so the page underneath retains
  // normal hover, click and focus behavior.
  delegate->set_accept_events(false);
  delegate->set_focus_traversable_from_anchor_view(false);
  delegate->set_close_on_deactivate(false);
  delegate->set_margins(gfx::Insets());
  delegate->SetContentsView(std::move(image_view));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(delegate.get());
  if (!widget) {
    preview_image_view_ = nullptr;
    return;
  }
  bubble_delegate_ = std::move(delegate);
  bubble_widget_ = std::move(widget);
  // Client-owned synchronous close removes the old widget from our state
  // before a rapid enter event for the next row can attempt to update it.
  // Without this, Close() may remain pending and swallow the next preview.
  bubble_widget_->MakeCloseSynchronous(
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&SidebarTabPreviewController::OnBubbleClosed,
                         weak_ptr_factory_.GetWeakPtr())));
  bubble_widget_->ShowInactive();
}

void SidebarTabPreviewController::CloseBubble() {
  if (bubble_widget_) {
    bubble_widget_->Close();
  }
}

void SidebarTabPreviewController::OnBubbleClosed() {
  preview_image_view_ = nullptr;
  // The delegate must outlive the Widget. Reset in that exact order; this is
  // the close callback contract for CLIENT_OWNS_WIDGET.
  bubble_widget_.reset();
  bubble_delegate_.reset();
}

}  // namespace ahoi::sidebar
