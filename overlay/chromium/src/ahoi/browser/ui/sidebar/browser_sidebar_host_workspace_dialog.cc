// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "ahoi/browser/ui/modal_overlay_controller.h"
#include "ahoi/browser/ui/sidebar/browser_sidebar_host_view.h"
#include "ahoi/browser/ui/sidebar/move_destination_menu_model.h"
#include "ahoi/browser/ui/sidebar/sidebar_action_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_drag_image.h"
#include "ahoi/browser/ui/sidebar/sidebar_recent_links_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_runtime_tab_views.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

const tab_tree::Workspace* BrowserSidebarHostView::FindWorkspace(
    const base::Uuid& workspace_id) const {
  auto workspace = std::ranges::find(workspace_service_->ordered_workspaces(),
                                     workspace_id, &tab_tree::Workspace::id);
  return workspace == workspace_service_->ordered_workspaces().end()
             ? nullptr
             : &*workspace;
}

void BrowserSidebarHostView::ShowWorkspaceDialog(
    PendingWorkspaceAction action,
    std::optional<base::Uuid> workspace_id) {
  CHECK(action != PendingWorkspaceAction::kNone);
  CHECK_EQ(action != PendingWorkspaceAction::kCreate, workspace_id.has_value());
  if (workspace_dialog_widget_ || group_dialog_widget_) {
    return;
  }

  const tab_tree::Workspace* existing =
      workspace_id.has_value() ? FindWorkspace(*workspace_id) : nullptr;
  if (action != PendingWorkspaceAction::kCreate && !existing) {
    return;
  }
  pending_workspace_action_ = action;
  pending_workspace_id_ = workspace_id;
  pending_workspace_accent_argb_ =
      existing ? existing->accent_argb : std::nullopt;

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  if (action == PendingWorkspaceAction::kDelete) {
    auto* warning = contents->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_DELETE_WORKSPACE_BODY)));
    warning->SetSubpixelRenderingEnabled(false);
    warning->SetMultiLine(true);
    warning->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  } else {
    auto* name_label = contents->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_NAME)));
    name_label->SetSubpixelRenderingEnabled(false);
    name_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_label->SetEnabledColor(visual_style::kMutedText);
    workspace_name_field_ =
        contents->AddChildView(std::make_unique<views::Textfield>());
    workspace_name_field_->SetText(
        existing ? existing->name
                 : l10n_util::GetStringUTF16(
                       IDS_AHOI_DIALOG_NEW_WORKSPACE_DEFAULT_NAME));
    workspace_name_field_->SetPlaceholderText(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_NAME));
    workspace_name_field_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_NAME));

    auto* icon_label = contents->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_ICON)));
    icon_label->SetSubpixelRenderingEnabled(false);
    icon_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    icon_label->SetEnabledColor(visual_style::kMutedText);
    workspace_icon_field_ =
        contents->AddChildView(std::make_unique<views::Textfield>());
    workspace_icon_field_->SetText(existing ? existing->icon : u"N");
    workspace_icon_field_->SetPlaceholderText(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_ICON));
    workspace_icon_field_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_WORKSPACE_ICON));

    auto* color_label = contents->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_COLOR)));
    color_label->SetSubpixelRenderingEnabled(false);
    color_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    color_label->SetEnabledColor(visual_style::kMutedText);
    auto color_choices = std::make_unique<views::View>();
    color_choices->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    const auto add_color_choice = [&](std::optional<uint32_t> color,
                                      int label_id) {
      auto button = CreateGroupColorSwatchButton(
          base::BindRepeating(&BrowserSidebarHostView::SelectWorkspaceColor,
                              weak_ptr_factory_.GetWeakPtr(), color),
          color, l10n_util::GetStringUTF16(label_id));
      views::Button* raw_button = button.get();
      workspace_color_buttons_.emplace_back(raw_button, color);
      color_choices->AddChildView(std::move(button));
    };
    add_color_choice(std::nullopt, IDS_AHOI_GROUP_COLOR_NONE);
    add_color_choice(visual_style::kUserAccentRed, IDS_AHOI_GROUP_COLOR_RED);
    add_color_choice(visual_style::kUserAccentOrange,
                     IDS_AHOI_GROUP_COLOR_ORANGE);
    add_color_choice(visual_style::kUserAccentYellow,
                     IDS_AHOI_GROUP_COLOR_YELLOW);
    add_color_choice(visual_style::kUserAccentGreen,
                     IDS_AHOI_GROUP_COLOR_GREEN);
    add_color_choice(visual_style::kUserAccentBlue, IDS_AHOI_GROUP_COLOR_BLUE);
    add_color_choice(visual_style::kUserAccentViolet,
                     IDS_AHOI_GROUP_COLOR_VIOLET);
    contents->AddChildView(std::move(color_choices));
    UpdateWorkspaceColorButtons();
  }

  views::View* const modal_anchor = modal_overlay_controller_->center_anchor();
  if (!modal_anchor || !modal_anchor->GetWidget()) {
    OnWorkspaceDialogClosed();
    return;
  }
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      modal_anchor, views::BubbleBorder::FLOAT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  const int title_id = action == PendingWorkspaceAction::kCreate ||
                               action == PendingWorkspaceAction::kDuplicate
                           ? IDS_AHOI_DIALOG_NEW_WORKSPACE
                       : action == PendingWorkspaceAction::kEdit
                           ? IDS_AHOI_DIALOG_EDIT_WORKSPACE
                           : IDS_AHOI_DIALOG_DELETE_WORKSPACE;
  delegate->SetTitle(l10n_util::GetStringUTF16(title_id));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                       static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetButtonLabel(
      ui::mojom::DialogButton::kOk,
      l10n_util::GetStringUTF16(
          action == PendingWorkspaceAction::kCreate ||
                  action == PendingWorkspaceAction::kDuplicate
              ? IDS_AHOI_DIALOG_CREATE
          : action == PendingWorkspaceAction::kEdit ? IDS_AHOI_DIALOG_SAVE
                                                    : IDS_DELETE));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                           l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_CANCEL));
  delegate->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<BrowserSidebarHostView> host) {
        if (!host) {
          return true;
        }
        if (!host->AcceptWorkspaceDialog()) {
          return false;
        }
        return !host || !host->RequestWorkspaceDialogClose();
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetCancelCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<BrowserSidebarHostView> host) {
        return !host || !host->RequestWorkspaceDialogClose();
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetBackgroundColor(visual_style::kChromeSurface);
  delegate->set_close_on_deactivate(false);
  delegate->set_fixed_width(visual_style::kSidebarDialogWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kSidebarDialogInset,
                                        visual_style::kSidebarDialogInset));
  if (workspace_name_field_) {
    delegate->SetInitiallyFocusedView(workspace_name_field_);
  }
  delegate->SetContentsView(std::move(contents));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&BrowserSidebarHostView::OnWorkspaceDialogClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    OnWorkspaceDialogClosed();
    return;
  }
  workspace_dialog_delegate_ = std::move(delegate);
  workspace_dialog_widget_ = std::move(widget);
  if (!modal_overlay_controller_->ShowPanel(
          workspace_dialog_widget_.get(),
          base::BindRepeating(&BrowserSidebarHostView::CloseWorkspaceDialogNow,
                              weak_ptr_factory_.GetWeakPtr()))) {
    workspace_name_field_ = nullptr;
    workspace_icon_field_ = nullptr;
    workspace_dialog_widget_.reset();
    workspace_dialog_delegate_.reset();
    pending_workspace_action_ = PendingWorkspaceAction::kNone;
    pending_workspace_id_.reset();
    pending_workspace_accent_argb_.reset();
    workspace_color_buttons_.clear();
    return;
  }
  if (workspace_name_field_) {
    workspace_name_field_->RequestFocus();
    workspace_name_field_->SelectAll(false);
  }
}

void BrowserSidebarHostView::SelectWorkspaceColor(std::optional<uint32_t> color,
                                                  const ui::Event&) {
  pending_workspace_accent_argb_ = color;
  UpdateWorkspaceColorButtons();
}

void BrowserSidebarHostView::UpdateWorkspaceColorButtons() {
  for (auto& [button, color] : workspace_color_buttons_) {
    const bool selected = pending_workspace_accent_argb_ == color;
    SetGroupColorSwatchSelected(button, selected);
  }
}

bool BrowserSidebarHostView::AcceptWorkspaceDialog() {
  if (pending_workspace_action_ == PendingWorkspaceAction::kDelete) {
    if (pending_workspace_id_.has_value()) {
      const tab_tree::TabTreeStore::Result result =
          session_bridge_->DeleteWorkspace(*pending_workspace_id_);
      if (result != tab_tree::TabTreeStore::Result::kOk) {
        OnMutationFailed(result);
        return false;
      }
    }
    return true;
  }
  if (!workspace_name_field_ || !workspace_icon_field_) {
    return false;
  }
  std::u16string name(workspace_name_field_->GetText());
  std::u16string icon(workspace_icon_field_->GetText());
  base::TrimWhitespace(name, base::TRIM_ALL, &name);
  base::TrimWhitespace(icon, base::TRIM_ALL, &icon);
  if (name.empty()) {
    return false;
  }
  if (icon.empty()) {
    icon = name.substr(0, std::min<size_t>(1, name.size()));
  }
  if (pending_workspace_action_ == PendingWorkspaceAction::kCreate) {
    const std::optional<base::Uuid> workspace_id =
        session_bridge_->CreateWorkspace(std::move(name), std::move(icon),
                                         pending_workspace_accent_argb_);
    if (!workspace_id.has_value()) {
      OnMutationFailed(tab_tree::TabTreeStore::Result::kDatabaseError);
      return false;
    }
    std::ignore = session_bridge_->SetActiveWorkspaceForWindow(
        browser_, *workspace_id, WorkspaceActivationSource::kSidebar);
    return true;
  }
  if (pending_workspace_action_ == PendingWorkspaceAction::kDuplicate &&
      pending_workspace_id_.has_value()) {
    const std::optional<base::Uuid> workspace_id =
        session_bridge_->DuplicateWorkspace(*pending_workspace_id_,
                                            std::move(name), std::move(icon),
                                            pending_workspace_accent_argb_);
    if (!workspace_id.has_value()) {
      OnMutationFailed(tab_tree::TabTreeStore::Result::kDatabaseError);
      return false;
    }
    std::ignore = session_bridge_->SetActiveWorkspaceForWindow(
        browser_, *workspace_id, WorkspaceActivationSource::kSidebar);
    return true;
  }
  if (pending_workspace_action_ == PendingWorkspaceAction::kEdit &&
      pending_workspace_id_.has_value()) {
    const tab_tree::TabTreeStore::Result result =
        session_bridge_->UpdateWorkspacePresentation(
            *pending_workspace_id_, std::move(name), std::move(icon),
            pending_workspace_accent_argb_);
    if (result != tab_tree::TabTreeStore::Result::kOk) {
      OnMutationFailed(result);
      return false;
    }
    return true;
  }
  return false;
}

bool BrowserSidebarHostView::RequestWorkspaceDialogClose() {
  return workspace_dialog_widget_ && modal_overlay_controller_->RequestClose(
                                         workspace_dialog_widget_.get());
}

void BrowserSidebarHostView::CloseWorkspaceDialogNow() {
  if (workspace_dialog_widget_) {
    workspace_dialog_widget_->Close();
  }
}

void BrowserSidebarHostView::OnWorkspaceDialogClosed() {
  if (workspace_dialog_widget_) {
    modal_overlay_controller_->NotifyPanelClosed(
        workspace_dialog_widget_.get());
  }
  pending_workspace_action_ = PendingWorkspaceAction::kNone;
  pending_workspace_id_.reset();
  pending_workspace_accent_argb_.reset();
  workspace_color_buttons_.clear();
  workspace_name_field_ = nullptr;
  workspace_icon_field_ = nullptr;
  std::unique_ptr<views::Widget> closed_widget =
      std::move(workspace_dialog_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(workspace_dialog_delegate_);
  if (closed_widget || closed_delegate) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<views::Widget> widget,
               std::unique_ptr<views::BubbleDialogDelegate> delegate) {
              widget.reset();
              delegate.reset();
            },
            std::move(closed_widget), std::move(closed_delegate)));
  }
}

}  // namespace ahoi::sidebar
