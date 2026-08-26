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
#include "ahoi/browser/ui/sidebar/sidebar_link_copy.h"
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

void BrowserSidebarHostView::ShowCreateGroupDialog(
    const base::Uuid& source_node_id) {
  if (!source_node_id.is_valid()) {
    return;
  }
  ShowGroupDialog(PendingGroupAction::kWrapNode, source_node_id, std::nullopt,
                  std::nullopt,
                  l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_GROUP));
}

void BrowserSidebarHostView::ShowGroupCustomizationDialog(
    const base::Uuid& folder_node_id) {
  const tab_tree::TreeNode* node =
      controller_->view_model().GetNode(folder_node_id);
  if (!node || node->type != tab_tree::TreeNodeType::kFolder) {
    return;
  }
  ShowGroupDialog(PendingGroupAction::kEditFolder, folder_node_id, std::nullopt,
                  std::nullopt, node->title);
}

void BrowserSidebarHostView::CopyAllLinksInGroup(
    const base::Uuid& folder_node_id) {
  const tab_tree::TreeNode* folder =
      controller_->view_model().GetNode(folder_node_id);
  if (!folder || folder->type != tab_tree::TreeNodeType::kFolder) {
    return;
  }
  std::u16string links;
  if (BuildOrderedLinkList(session_bridge_->tab_tree_store(),
                           folder->workspace_id, folder_node_id,
                           &links) != tab_tree::TabTreeStore::Result::kOk ||
      links.empty()) {
    return;
  }
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(links);
}

void BrowserSidebarHostView::CopyAllLinksInWorkspace(
    const base::Uuid& workspace_id) {
  std::u16string links;
  if (BuildOrderedLinkList(session_bridge_->tab_tree_store(), workspace_id,
                           std::nullopt,
                           &links) != tab_tree::TabTreeStore::Result::kOk ||
      links.empty()) {
    return;
  }
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(links);
}

void BrowserSidebarHostView::ShowCreateGroupDialogForTemporaryTab(
    int runtime_tab_handle) {
  if (!FindTemporaryTab(runtime_tab_handle)) {
    return;
  }
  OnTemporaryTabDragStateChanged(std::nullopt);
  ShowGroupDialog(PendingGroupAction::kWrapTemporaryTab, std::nullopt,
                  std::nullopt, runtime_tab_handle,
                  l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_GROUP));
}

void BrowserSidebarHostView::ShowCreateRootGroupDialog() {
  ShowGroupDialog(PendingGroupAction::kCreateFolder, std::nullopt, std::nullopt,
                  std::nullopt,
                  l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_GROUP));
}

void BrowserSidebarHostView::ShowCreateSubgroupDialog(
    const base::Uuid& parent_node_id) {
  if (!parent_node_id.is_valid()) {
    return;
  }
  ShowGroupDialog(PendingGroupAction::kCreateFolder, std::nullopt,
                  parent_node_id, std::nullopt,
                  l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_SUBGROUP));
}

void BrowserSidebarHostView::ShowGroupDialog(
    PendingGroupAction action,
    std::optional<base::Uuid> source_node_id,
    std::optional<base::Uuid> parent_node_id,
    std::optional<int> runtime_tab_handle,
    const std::u16string& default_title) {
  CHECK(action != PendingGroupAction::kNone);
  CHECK_EQ(action == PendingGroupAction::kWrapNode ||
               action == PendingGroupAction::kEditFolder,
           source_node_id.has_value());
  CHECK_EQ(action == PendingGroupAction::kWrapTemporaryTab,
           runtime_tab_handle.has_value());
  CHECK(!parent_node_id.has_value() ||
        action == PendingGroupAction::kCreateFolder);
  OnSidebarDragStateChanged(std::nullopt);
  OnTemporaryTabDragStateChanged(std::nullopt);
  if (group_dialog_widget_) {
    group_dialog_widget_->Close();
  }
  CHECK(!group_dialog_widget_);
  CHECK(!group_dialog_delegate_);

  pending_group_action_ = action;
  pending_group_source_id_ = source_node_id;
  pending_group_parent_id_ = parent_node_id;
  pending_group_runtime_tab_handle_ = runtime_tab_handle;
  pending_group_icon_ = u"folder";
  pending_group_accent_argb_.reset();
  if (action == PendingGroupAction::kEditFolder && source_node_id.has_value()) {
    const tab_tree::TreeNode* node =
        controller_->view_model().GetNode(*source_node_id);
    if (!node || node->type != tab_tree::TreeNodeType::kFolder) {
      pending_group_action_ = PendingGroupAction::kNone;
      return;
    }
    pending_group_icon_ = node->icon.empty() ? u"folder" : node->icon;
    pending_group_accent_argb_ = node->accent_argb;
  }
  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  group_name_field_ =
      contents->AddChildView(std::make_unique<views::Textfield>());
  group_name_field_->SetText(default_title);
  const std::u16string group_name =
      l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_NAME_PLACEHOLDER);
  group_name_field_->SetPlaceholderText(group_name);
  group_name_field_->GetViewAccessibility().SetName(group_name);

  auto* icon_label = contents->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_ICON)));
  icon_label->SetSubpixelRenderingEnabled(false);
  icon_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  icon_label->SetEnabledColor(visual_style::kMutedText);
  auto icon_choices = std::make_unique<views::View>();
  icon_choices->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  const auto add_icon_choice = [&](std::u16string id,
                                   const gfx::VectorIcon& vector_icon,
                                   std::u16string accessible_name) {
    auto button = views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(&BrowserSidebarHostView::SelectGroupIcon,
                            weak_ptr_factory_.GetWeakPtr(), id),
        vector_icon, 18, visual_style::kMutedText, visual_style::kDisabledIcon,
        visual_style::kText);
    button->SetAccessibleName(accessible_name);
    button->SetTooltipText(accessible_name);
    button->SetPreferredSize(gfx::Size(42, 34));
    views::ImageButton* raw_button = button.get();
    group_icon_buttons_.emplace_back(raw_button, std::move(id));
    icon_choices->AddChildView(std::move(button));
  };
  add_icon_choice(u"folder", vector_icons::kFolderFlippableIcon,
                  l10n_util::GetStringUTF16(IDS_AHOI_GROUP_ICON_FOLDER));
  add_icon_choice(u"code", vector_icons::kCodeIcon,
                  l10n_util::GetStringUTF16(IDS_AHOI_GROUP_ICON_CODE));
  add_icon_choice(u"lock", vector_icons::kLockIcon,
                  l10n_util::GetStringUTF16(IDS_AHOI_GROUP_ICON_PRIVATE));
  add_icon_choice(u"archive", vector_icons::kDatabaseIcon,
                  l10n_util::GetStringUTF16(IDS_AHOI_GROUP_ICON_ARCHIVE));
  add_icon_choice(u"moon", vector_icons::kAccountCircleIcon,
                  l10n_util::GetStringUTF16(IDS_AHOI_GROUP_ICON_PERSONAL));
  contents->AddChildView(std::move(icon_choices));

  const std::u16string custom_icon_name =
      l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_CUSTOM_ICON);
  auto* custom_icon_label =
      contents->AddChildView(std::make_unique<views::Label>(custom_icon_name));
  custom_icon_label->SetSubpixelRenderingEnabled(false);
  custom_icon_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  custom_icon_label->SetEnabledColor(visual_style::kMutedText);
  group_icon_field_ =
      contents->AddChildView(std::make_unique<views::Textfield>());
  group_icon_field_->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_CUSTOM_ICON_PLACEHOLDER));
  group_icon_field_->SetAccessibleName(custom_icon_name);
  group_icon_field_->SetTooltipText(custom_icon_name);
  group_icon_field_->SetController(this);
  if (pending_group_icon_ != u"folder" && pending_group_icon_ != u"code" &&
      pending_group_icon_ != u"lock" && pending_group_icon_ != u"archive" &&
      pending_group_icon_ != u"moon") {
    group_icon_field_->SetText(pending_group_icon_);
  }

  auto* color_label = contents->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_GROUP_COLOR)));
  color_label->SetSubpixelRenderingEnabled(false);
  color_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  color_label->SetEnabledColor(visual_style::kMutedText);
  auto color_choices = std::make_unique<views::View>();
  color_choices->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  const auto add_color_choice = [&](std::optional<uint32_t> color,
                                    std::u16string accessible_name) {
    auto button = CreateGroupColorSwatchButton(
        base::BindRepeating(&BrowserSidebarHostView::SelectGroupColor,
                            weak_ptr_factory_.GetWeakPtr(), color),
        color, std::move(accessible_name));
    views::Button* raw_button = button.get();
    group_color_buttons_.emplace_back(raw_button, color);
    color_choices->AddChildView(std::move(button));
  };
  add_color_choice(std::nullopt,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_NONE));
  add_color_choice(visual_style::kUserAccentRed,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_RED));
  add_color_choice(visual_style::kUserAccentOrange,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_ORANGE));
  add_color_choice(visual_style::kUserAccentYellow,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_YELLOW));
  add_color_choice(visual_style::kUserAccentGreen,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_GREEN));
  add_color_choice(visual_style::kUserAccentBlue,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_BLUE));
  add_color_choice(visual_style::kUserAccentViolet,
                   l10n_util::GetStringUTF16(IDS_AHOI_GROUP_COLOR_VIOLET));
  color_choices->AddChildView(std::make_unique<views::View>());
  contents->AddChildView(std::move(color_choices));
  UpdateGroupChoiceButtons();

  views::View* const modal_anchor = modal_overlay_controller_->center_anchor();
  if (!modal_anchor || !modal_anchor->GetWidget()) {
    pending_group_action_ = PendingGroupAction::kNone;
    pending_group_source_id_.reset();
    pending_group_parent_id_.reset();
    pending_group_runtime_tab_handle_.reset();
    pending_group_icon_.clear();
    pending_group_accent_argb_.reset();
    group_icon_buttons_.clear();
    group_color_buttons_.clear();
    group_name_field_ = nullptr;
    group_icon_field_ = nullptr;
    return;
  }
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      modal_anchor, views::BubbleBorder::FLOAT,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetTitle(action == PendingGroupAction::kEditFolder
                         ? l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_EDIT_GROUP)
                         : default_title);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                       static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetButtonLabel(
      ui::mojom::DialogButton::kOk,
      l10n_util::GetStringUTF16(action == PendingGroupAction::kEditFolder
                                    ? IDS_AHOI_DIALOG_SAVE
                                    : IDS_AHOI_DIALOG_CREATE));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                           l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_CANCEL));
  delegate->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<BrowserSidebarHostView> host) {
        if (!host) {
          return true;
        }
        if (!host->AcceptCreateGroupDialog()) {
          return false;
        }
        // Returning false lets the shared overlay finish its fade before it
        // asks the client-owned Widget to close.
        return !host || !host->RequestGroupDialogClose();
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetCancelCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<BrowserSidebarHostView> host) {
        return !host || !host->RequestGroupDialogClose();
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate->SetBackgroundColor(visual_style::kChromeSurface);
  delegate->set_close_on_deactivate(false);
  delegate->set_fixed_width(visual_style::kSidebarDialogWidth);
  delegate->set_margins(gfx::Insets::VH(visual_style::kSidebarDialogInset,
                                        visual_style::kSidebarDialogInset));
  delegate->SetInitiallyFocusedView(group_name_field_);
  delegate->SetContentsView(std::move(contents));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&BrowserSidebarHostView::OnCreateGroupDialogClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    pending_group_action_ = PendingGroupAction::kNone;
    pending_group_source_id_.reset();
    pending_group_parent_id_.reset();
    pending_group_runtime_tab_handle_.reset();
    group_name_field_ = nullptr;
    group_icon_field_ = nullptr;
    return;
  }
  group_dialog_delegate_ = std::move(delegate);
  group_dialog_widget_ = std::move(widget);
  if (!modal_overlay_controller_->ShowPanel(
          group_dialog_widget_.get(),
          base::BindRepeating(&BrowserSidebarHostView::CloseGroupDialogNow,
                              weak_ptr_factory_.GetWeakPtr()))) {
    group_name_field_ = nullptr;
    group_icon_field_ = nullptr;
    group_dialog_widget_.reset();
    group_dialog_delegate_.reset();
    pending_group_action_ = PendingGroupAction::kNone;
    pending_group_source_id_.reset();
    pending_group_parent_id_.reset();
    pending_group_runtime_tab_handle_.reset();
    return;
  }
  if (group_name_field_) {
    group_name_field_->RequestFocus();
    group_name_field_->SelectAll(false);
  }
}

void BrowserSidebarHostView::SelectGroupIcon(std::u16string icon,
                                             const ui::Event&) {
  if (group_icon_field_) {
    group_icon_field_->SetText(std::u16string());
  }
  pending_group_icon_ = std::move(icon);
  UpdateGroupChoiceButtons();
}

void BrowserSidebarHostView::SelectGroupColor(std::optional<uint32_t> color,
                                              const ui::Event&) {
  pending_group_accent_argb_ = color;
  UpdateGroupChoiceButtons();
}

// views::TextfieldController:
void BrowserSidebarHostView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender != group_icon_field_) {
    return;
  }
  std::u16string custom_icon = new_contents;
  base::TrimWhitespace(custom_icon, base::TRIM_ALL, &custom_icon);
  if (!custom_icon.empty()) {
    pending_group_icon_ = std::move(custom_icon);
  } else {
    pending_group_icon_ = u"folder";
  }
  UpdateGroupChoiceButtons();
}

void BrowserSidebarHostView::UpdateGroupChoiceButtons() {
  for (auto& [button, icon] : group_icon_buttons_) {
    const bool selected = icon == pending_group_icon_;
    button->SetBackground(selected ? views::CreateRoundedRectBackground(
                                         visual_style::kSelectedSurface,
                                         visual_style::kControlCornerRadius)
                                   : nullptr);
    button->SetBorder(views::CreateRoundedRectBorder(
        1, visual_style::kControlCornerRadius,
        selected ? visual_style::kAccent : visual_style::kDivider));
  }
  for (auto& [button, color] : group_color_buttons_) {
    const bool selected = color == pending_group_accent_argb_;
    SetGroupColorSwatchSelected(button, selected);
  }
}

bool BrowserSidebarHostView::AcceptCreateGroupDialog() {
  if (!group_name_field_) {
    return false;
  }
  if (group_icon_field_) {
    std::u16string custom_icon(group_icon_field_->GetText());
    base::TrimWhitespace(custom_icon, base::TRIM_ALL, &custom_icon);
    if (!custom_icon.empty()) {
      pending_group_icon_ = std::move(custom_icon);
    }
  }
  std::u16string title(group_name_field_->GetText());
  base::TrimWhitespace(title, base::TRIM_ALL, &title);
  if (title.empty()) {
    return false;
  }
  if (pending_group_action_ == PendingGroupAction::kWrapNode &&
      pending_group_source_id_.has_value()) {
    CreateGroupAroundNode(*pending_group_source_id_, title);
  } else if (pending_group_action_ == PendingGroupAction::kWrapTemporaryTab &&
             pending_group_runtime_tab_handle_.has_value()) {
    base::Uuid saved_node_id;
    if (SaveTemporaryTabAtWorkspaceRoot(*pending_group_runtime_tab_handle_,
                                        &saved_node_id)) {
      CreateGroupAroundNode(saved_node_id, title);
    } else {
      return false;
    }
  } else if (pending_group_action_ == PendingGroupAction::kCreateFolder) {
    CreateFolder(pending_group_parent_id_, title);
  } else if (pending_group_action_ == PendingGroupAction::kEditFolder &&
             pending_group_source_id_.has_value()) {
    std::u16string normalized_title = title;
    const tab_tree::TabTreeStore::Result result =
        controller_->UpdateFolderPresentation(
            *pending_group_source_id_, std::move(normalized_title),
            pending_group_icon_, pending_group_accent_argb_, base::Time::Now());
    if (result != tab_tree::TabTreeStore::Result::kOk) {
      OnMutationFailed(result);
    }
  }
  return true;
}

bool BrowserSidebarHostView::RequestGroupDialogClose() {
  return group_dialog_widget_ &&
         modal_overlay_controller_->RequestClose(group_dialog_widget_.get());
}

void BrowserSidebarHostView::CloseGroupDialogNow() {
  if (group_dialog_widget_) {
    group_dialog_widget_->Close();
  }
}

void BrowserSidebarHostView::OnCreateGroupDialogClosed() {
  if (group_dialog_widget_) {
    modal_overlay_controller_->NotifyPanelClosed(group_dialog_widget_.get());
  }
  pending_group_action_ = PendingGroupAction::kNone;
  pending_group_source_id_.reset();
  pending_group_parent_id_.reset();
  pending_group_runtime_tab_handle_.reset();
  pending_group_icon_.clear();
  pending_group_accent_argb_.reset();
  group_icon_buttons_.clear();
  group_color_buttons_.clear();
  group_name_field_ = nullptr;
  group_icon_field_ = nullptr;

  // A close callback can run from inside the Widget observer iteration
  // (notably OnWidgetActivationChanged on macOS). Destroying the Widget here
  // invalidates that observer list while it is being traversed. Tear the
  // client-owned bubble down on the next task instead, and keep its delegate
  // alive until after the Widget because BubbleDialogDelegate requires that
  // lifetime ordering.
  std::unique_ptr<views::Widget> closed_widget =
      std::move(group_dialog_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(group_dialog_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       widget.reset();
                       delegate.reset();
                     },
                     std::move(closed_widget), std::move(closed_delegate)));
}

void BrowserSidebarHostView::CreateGroupAroundNode(
    const base::Uuid& source_node_id,
    std::u16string title) {
  base::TrimWhitespace(title, base::TRIM_ALL, &title);
  if (title.empty()) {
    title = l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_GROUP);
  }
  base::Uuid folder_id;
  const std::vector<base::Uuid> source_node_ids =
      GetMoveGroupNodeIds(source_node_id);
  const tab_tree::TabTreeStore::Result result =
      controller_->CreateGroupAroundNodes(
          source_node_ids, std::move(title), base::Time::Now(), &folder_id,
          pending_group_icon_, pending_group_accent_argb_);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
    return;
  }
  std::ignore = controller_->SelectNode(folder_id);
  std::ignore = controller_->ExpandNode(folder_id);
}

void BrowserSidebarHostView::CreateFolder(
    std::optional<base::Uuid> parent_node_id,
    std::u16string title) {
  base::TrimWhitespace(title, base::TRIM_ALL, &title);
  if (title.empty()) {
    title = parent_node_id.has_value()
                ? l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_SUBGROUP)
                : l10n_util::GetStringUTF16(IDS_AHOI_DIALOG_NEW_GROUP);
  }
  base::Uuid folder_id;
  const tab_tree::TabTreeStore::Result result = controller_->CreateFolder(
      parent_node_id, std::move(title), base::Time::Now(), &folder_id,
      pending_group_icon_, pending_group_accent_argb_);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
    return;
  }
  if (parent_node_id.has_value()) {
    std::ignore = controller_->ExpandNode(*parent_node_id);
  }
  std::ignore = controller_->SelectNode(folder_id);
  std::ignore = controller_->ExpandNode(folder_id);
}

}  // namespace ahoi::sidebar
