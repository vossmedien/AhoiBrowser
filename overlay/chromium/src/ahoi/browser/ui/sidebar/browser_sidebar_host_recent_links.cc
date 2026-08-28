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

void BrowserSidebarHostView::OnFaviconAvailable(
    const GURL& page_url,
    const favicon_base::FaviconImageResult& result) {
  if (!result.image.IsEmpty()) {
    const ui::ImageModel favicon = ui::ImageModel::FromImage(result.image);
    favicon_cache_.insert_or_assign(page_url, favicon);
    if (tree_view_) {
      tree_view_->OnRuntimePresentationChanged();
    }
    if (group_recent_links_view_) {
      UpdateGroupRecentLinkFavicon(group_recent_links_view_, page_url, favicon);
    }
  }
}

void BrowserSidebarHostView::OnFolderHoverChanged(
    const base::Uuid& folder_node_id,
    views::View* anchor,
    bool hovered) {
  if (!folder_node_id.is_valid() || !anchor) {
    return;
  }
  if (hovered) {
    if (tab_preview_controller_) {
      tab_preview_controller_->Hide();
    }
    hovered_folder_id_ = folder_node_id;
    group_recent_anchor_tracker_.SetView(anchor);
    group_recent_hide_timer_.Stop();
    if (group_recent_bubble_folder_id_ == folder_node_id &&
        group_recent_widget_) {
      return;
    }
    group_recent_show_timer_.Start(
        FROM_HERE, visual_style::kRecentLinksHoverOpenDelay,
        base::BindOnce(&BrowserSidebarHostView::BeginGroupRecentQuery,
                       weak_ptr_factory_.GetWeakPtr(), folder_node_id));
    return;
  }
  if (hovered_folder_id_ == folder_node_id) {
    hovered_folder_id_.reset();
    group_recent_show_timer_.Stop();
    ScheduleGroupRecentBubbleHide();
  }
}

void BrowserSidebarHostView::BeginGroupRecentQuery(
    const base::Uuid& folder_node_id) {
  if (hovered_folder_id_ != folder_node_id || !group_recent_anchor_tracker_) {
    return;
  }
  auto* anchor = views::AsViewClass<SidebarTreeRowView>(
      group_recent_anchor_tracker_.view());
  if (!anchor || !anchor->is_bound() || anchor->node_id() != folder_node_id) {
    return;
  }

  CloseGroupRecentBubble();
  group_recent_history_task_tracker_.TryCancelAll();
  const uint64_t generation = ++group_recent_query_generation_;

  std::vector<tab_tree::TreeNode> subtree;
  const tab_tree::TabTreeStore::Result result =
      session_bridge_->tab_tree_store()->GetSubtree(folder_node_id, &subtree);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return;
  }

  std::map<GURL, tab_tree::TreeNode> pages_by_url;
  for (tab_tree::TreeNode& node : subtree) {
    if (!node.tombstone && node.type == tab_tree::TreeNodeType::kSavedPage &&
        node.url.is_valid() && !node.url.is_empty()) {
      pages_by_url.try_emplace(node.url, std::move(node));
    }
  }
  if (!history_service_ || pages_by_url.empty()) {
    ShowGroupRecentBubble(folder_node_id, {});
    return;
  }

  // One bounded recent-history query replaces one asynchronous database query
  // per saved page. Large nested groups therefore remain cheap to hover while
  // preserving the same recent-first result semantics.
  history::QueryOptions options;
  options.SetRecentDayRange(365);
  options.max_count = 2048;
  options.duplicate_policy = history::QueryOptions::REMOVE_ALL_DUPLICATES;
  options.visit_order = history::QueryOptions::RECENT_FIRST;
  history_service_->QueryHistory(
      std::u16string(), options,
      base::BindOnce(&BrowserSidebarHostView::OnGroupHistoryQueryCompleted,
                     weak_ptr_factory_.GetWeakPtr(), generation, folder_node_id,
                     std::move(pages_by_url)),
      &group_recent_history_task_tracker_);
}

void BrowserSidebarHostView::OnGroupHistoryQueryCompleted(
    uint64_t generation,
    const base::Uuid& folder_node_id,
    std::map<GURL, tab_tree::TreeNode> pages_by_url,
    history::QueryResults results) {
  if (generation != group_recent_query_generation_ ||
      hovered_folder_id_ != folder_node_id || !group_recent_anchor_tracker_) {
    return;
  }

  std::vector<RecentGroupLink> links;
  constexpr size_t kMaximumRecentLinks = 6u;
  links.reserve(kMaximumRecentLinks);
  for (const history::URLResult& result : results) {
    auto page = pages_by_url.find(result.url());
    if (page == pages_by_url.end() || result.visit_time().is_null()) {
      continue;
    }
    const tab_tree::TreeNode& node = page->second;
    links.push_back({.node_id = node.id,
                     .title = node.title,
                     .url = node.url,
                     .last_visit = result.visit_time(),
                     .favicon = GetSavedPageIcon(node)});
    pages_by_url.erase(page);
    if (links.size() == kMaximumRecentLinks) {
      break;
    }
  }
  ShowGroupRecentBubble(folder_node_id, std::move(links));
}

void BrowserSidebarHostView::ShowGroupRecentBubble(
    const base::Uuid& folder_node_id,
    std::vector<RecentGroupLink> links) {
  if (hovered_folder_id_ != folder_node_id || !group_recent_anchor_tracker_ ||
      group_recent_widget_) {
    return;
  }
  auto* anchor = views::AsViewClass<SidebarTreeRowView>(
      group_recent_anchor_tracker_.view());
  const tab_tree::TreeNode* folder =
      controller_->view_model().GetNode(folder_node_id);
  if (!anchor || !anchor->is_bound() || anchor->node_id() != folder_node_id ||
      !folder) {
    return;
  }

  auto contents = CreateGroupRecentLinksView(
      std::move(links),
      base::BindRepeating(&BrowserSidebarHostView::ActivateRecentGroupLink,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&BrowserSidebarHostView::OnGroupRecentBubbleHover,
                          weak_ptr_factory_.GetWeakPtr()));
  group_recent_links_view_ = contents.get();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::LEFT_CENTER,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetAccessibleTitle(folder->title);
  delegate->SetBackgroundColor(visual_style::kChromeSurface);
  delegate->SetCanActivate(true);
  delegate->set_close_on_deactivate(false);
  delegate->set_fixed_width(visual_style::kSidebarDialogWidth);
  delegate->set_margins(
      gfx::Insets::VH(visual_style::kSidebarRecentLinksDialogInset,
                      visual_style::kSidebarRecentLinksDialogInset));
  delegate->SetContentsView(std::move(contents));

  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(
          delegate.get(),
          base::IgnoreArgs<views::Widget::ClosedReason>(
              base::BindOnce(&BrowserSidebarHostView::OnGroupRecentBubbleClosed,
                             weak_ptr_factory_.GetWeakPtr())));
  if (!widget) {
    group_recent_links_view_ = nullptr;
    return;
  }
  group_recent_bubble_folder_id_ = folder_node_id;
  group_recent_delegate_ = std::move(delegate);
  group_recent_widget_ = std::move(widget);
  group_recent_widget_->ShowInactive();
}

void BrowserSidebarHostView::ActivateRecentGroupLink(
    const base::Uuid& node_id) {
  tab_tree::TreeNode node;
  if (session_bridge_->tab_tree_store()->GetNode(node_id, &node) ==
          tab_tree::TabTreeStore::Result::kOk &&
      !node.tombstone && node.type == tab_tree::TreeNodeType::kSavedPage) {
    ActivateSavedPage(node);
  }
  InvalidateAndCloseGroupRecentBubble();
}

void BrowserSidebarHostView::OnGroupRecentBubbleHover(bool hovered) {
  group_recent_bubble_hovered_ = hovered;
  if (hovered) {
    group_recent_hide_timer_.Stop();
  } else {
    ScheduleGroupRecentBubbleHide();
  }
}

void BrowserSidebarHostView::ScheduleGroupRecentBubbleHide() {
  group_recent_hide_timer_.Start(
      FROM_HERE, visual_style::kRecentLinksHoverCloseDelay,
      base::BindOnce(&BrowserSidebarHostView::MaybeHideGroupRecentBubble,
                     weak_ptr_factory_.GetWeakPtr()));
}

void BrowserSidebarHostView::MaybeHideGroupRecentBubble() {
  if (!hovered_folder_id_.has_value() && !group_recent_bubble_hovered_) {
    InvalidateAndCloseGroupRecentBubble();
  }
}

void BrowserSidebarHostView::InvalidateAndCloseGroupRecentBubble() {
  ++group_recent_query_generation_;
  group_recent_history_task_tracker_.TryCancelAll();
  CloseGroupRecentBubble();
}

void BrowserSidebarHostView::CloseGroupRecentBubble() {
  group_recent_show_timer_.Stop();
  group_recent_hide_timer_.Stop();
  group_recent_bubble_hovered_ = false;
  if (group_recent_widget_) {
    group_recent_widget_->Close();
  }
}

void BrowserSidebarHostView::OnGroupRecentBubbleClosed() {
  group_recent_links_view_ = nullptr;
  group_recent_bubble_folder_id_.reset();
  std::unique_ptr<views::Widget> closed_widget =
      std::move(group_recent_widget_);
  std::unique_ptr<views::BubbleDialogDelegate> closed_delegate =
      std::move(group_recent_delegate_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::unique_ptr<views::Widget> widget,
                        std::unique_ptr<views::BubbleDialogDelegate> delegate) {
                       widget.reset();
                       delegate.reset();
                     },
                     std::move(closed_widget), std::move(closed_delegate)));
}

}  // namespace ahoi::sidebar
