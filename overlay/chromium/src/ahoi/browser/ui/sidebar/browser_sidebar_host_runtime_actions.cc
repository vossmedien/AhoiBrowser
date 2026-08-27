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
#include "ahoi/browser/ui/sidebar/sidebar_split_tab_operations.h"
#include "ahoi/browser/ui/sidebar/sidebar_tab_thumbnail_cache.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_delegate.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
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
#include "components/tabs/public/split_tab_collection.h"
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

bool BrowserSidebarHostView::CanDropOnRuntimeTab(
    std::optional<base::Uuid> source_node_id,
    std::optional<int> source_runtime_handle,
    base::WeakPtr<tabs::TabInterface> target,
    OpenTabDropPosition position) const {
  if (!target || !tab_strip_model_ ||
      source_node_id.has_value() == source_runtime_handle.has_value()) {
    return false;
  }

  tabs::TabInterface* source = nullptr;
  if (source_node_id.has_value()) {
    source = session_bridge_->FindTabByTreeNodeId(*source_node_id);
    // Closed saved pages can still be moved into the temporary section via
    // its free area, but row-relative ordering requires a live tab identity.
    if (!source) {
      return false;
    }
  } else {
    source = FindTemporaryTab(*source_runtime_handle);
  }
  if (!source ||
      session_bridge_->FindTabStripModelForTab(source) != tab_strip_model_ ||
      session_bridge_->FindTabStripModelForTab(target.get()) !=
          tab_strip_model_) {
    return false;
  }

  // Dropping onto the outer edge of the source pane is the only universally
  // available detach target: an all-temporary split may have no ordinary row
  // or saved-tree destination. Keep the center reserved for pane reordering.
  if (source == target.get()) {
    return CanDetachRuntimeSplitPaneOnSelfDrop(source->IsSplit(), position);
  }

  if (position != OpenTabDropPosition::kSplit) {
    return true;
  }
  if (source->IsSplit()) {
    return target->IsSplit() && source->GetSplit() == target->GetSplit();
  }
  if (!target->IsSplit()) {
    return true;
  }
  const split_tabs::SplitTabData* target_split =
      tab_strip_model_->GetSplitData(*target->GetSplit());
  // This guard mirrors Chromium's collection invariant; Ahoi does not add a
  // smaller split limit of its own.
  return target_split &&
         target_split->ListTabs().size() < tabs::SplitTabCollection::kMaxTabs;
}

bool BrowserSidebarHostView::DropOnRuntimeTab(
    std::optional<base::Uuid> source_node_id,
    std::optional<int> source_runtime_handle,
    base::WeakPtr<tabs::TabInterface> target,
    OpenTabDropPosition position) {
  // A successful reorder can synchronously rebuild the source row before its
  // native OnDragDone callback runs. Clear both possible source identities on
  // every callback exit so the split highlight and drag-only group target
  // cannot remain stuck after success or a late validation failure.
  base::ScopedClosureRunner clear_drag_presentation(base::BindOnce(
      [](base::WeakPtr<BrowserSidebarHostView> host) {
        if (!host) {
          return;
        }
        host->OnSidebarDragStateChanged(std::nullopt);
        host->OnTemporaryTabDragStateChanged(std::nullopt);
      },
      weak_ptr_factory_.GetWeakPtr()));
  if (!CanDropOnRuntimeTab(source_node_id, source_runtime_handle, target,
                           position)) {
    return false;
  }
  tabs::TabInterface* source =
      source_node_id.has_value()
          ? session_bridge_->FindTabByTreeNodeId(*source_node_id)
          : FindTemporaryTab(*source_runtime_handle);
  if (!source || !target) {
    return false;
  }

  if (position == OpenTabDropPosition::kSplit) {
    const bool same_split = source->GetSplit().has_value() &&
                            source->GetSplit() == target->GetSplit();
    const bool created =
        same_split ? tab_strip_model_->ReorderTabInSplit(source, target.get())
                   : tab_strip_model_
                         ->CreateOrAddToSplitFromDrop(
                             tab_strip_model_->GetIndexOfTab(source),
                             tab_strip_model_->GetIndexOfTab(target.get()),
                             split_tabs::SplitTabArrangement::kLinear)
                         .has_value();
    ScheduleRuntimePresentationRefresh();
    return created;
  }

  // A saved segment in a mixed composite row still owns its durable node.
  // Pulling it out of its own split must only change Chromium split
  // membership; it is not a move into the temporary section.
  const bool extracting_from_same_split =
      source->GetSplit().has_value() &&
      source->GetSplit() == target->GetSplit();
  if (source_node_id.has_value() && !extracting_from_same_split &&
      !MakeSavedPageTemporary(*source_node_id)) {
    return false;
  }
  if (source->IsSplit() &&
      !ExtractTabFromSplitPreservingRemainder(tab_strip_model_, source)) {
    return false;
  }
  const int source_index = tab_strip_model_->GetIndexOfTab(source);
  int target_index = tab_strip_model_->GetIndexOfTab(target.get());
  if (source_index < 0 || target_index < 0) {
    return false;
  }
  if (target->GetSplit().has_value()) {
    const split_tabs::SplitTabData* const target_split =
        tab_strip_model_->GetSplitData(*target->GetSplit());
    if (!target_split) {
      return false;
    }
    const gfx::Range target_range = target_split->GetIndexRange();
    target_index = position == OpenTabDropPosition::kBefore
                       ? static_cast<int>(target_range.start())
                       : static_cast<int>(target_range.end());
  } else if (position == OpenTabDropPosition::kAfter) {
    ++target_index;
  }
  if (source_index < target_index) {
    --target_index;
  }
  target_index = std::clamp(target_index, 0, tab_strip_model_->count() - 1);
  if (source_index != target_index) {
    tab_strip_model_->MoveWebContentsAt(source_index, target_index,
                                        /*select_after_move=*/false);
  }
  ScheduleRuntimePresentationRefresh();
  return true;
}

tabs::TabInterface* BrowserSidebarHostView::FindTemporaryTab(
    int runtime_tab_handle) const {
  if (!tab_strip_model_ || runtime_tab_handle < 0) {
    return nullptr;
  }
  for (tabs::TabInterface* tab : *tab_strip_model_) {
    if (tab && tab->GetHandle().raw_value() == runtime_tab_handle &&
        !session_bridge_->FindTreeNodeIdForTab(tab).has_value()) {
      return tab;
    }
  }
  return nullptr;
}

bool BrowserSidebarHostView::SaveTemporaryTabAtDrop(
    int runtime_tab_handle,
    const SidebarTreeController::DropTarget& target,
    base::Uuid* created_node_id) {
  tabs::TabInterface* tab = FindTemporaryTab(runtime_tab_handle);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!tab || !contents) {
    return false;
  }
  const bool extract_split_pane = tab->IsSplit();
  GURL url = contents->GetVisibleURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetLastCommittedURL();
  }
  if (!url.is_valid() || url.is_empty()) {
    url = GURL("about:blank");
  }
  const std::u16string title = tab->GetTitle().empty()
                                   ? l10n_util::GetStringUTF16(IDS_NEW_TAB)
                                   : tab->GetTitle();
  tab_tree::TreeNode created;
  const tab_tree::TabTreeStore::Result result =
      controller_->CreateSavedPageAtDrop(target, title, url, base::Time::Now(),
                                         &created);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    OnMutationFailed(result);
    return false;
  }
  if (!session_bridge_->BindTreeNodeToTab(created, tab)) {
    const tab_tree::TabTreeStore::Result rollback =
        controller_->DeleteNode(created.id, base::Time::Now());
    if (rollback != tab_tree::TabTreeStore::Result::kOk) {
      OnMutationFailed(rollback);
    }
    return false;
  }
  if (created.parent_id.has_value()) {
    std::ignore = controller_->ExpandNode(*created.parent_id);
  }
  std::ignore = controller_->SelectNode(created.id);
  if (created_node_id) {
    *created_node_id = created.id;
  }
  if (extract_split_pane) {
    CHECK(ExtractTabFromSplitPreservingRemainder(tab_strip_model_, tab));
  }
  OnTemporaryTabDragStateChanged(std::nullopt);
  ScheduleRuntimePresentationRefresh();
  return true;
}

bool BrowserSidebarHostView::SaveTemporaryTabAtWorkspaceRoot(
    int runtime_tab_handle,
    base::Uuid* created_node_id) {
  const std::optional<base::Uuid> workspace_id =
      controller_->view_model().workspace_id();
  return workspace_id.has_value() &&
         SaveTemporaryTabAtDrop(
             runtime_tab_handle,
             {.workspace_id = *workspace_id,
              .target_node_id = std::nullopt,
              .position = SidebarTreeController::DropPosition::kInside},
             created_node_id);
}

bool BrowserSidebarHostView::MakeSavedPageTemporary(
    const base::Uuid& source_node_id) {
  tab_tree::TreeNode node;
  if (session_bridge_->tab_tree_store()->GetNode(source_node_id, &node) !=
          tab_tree::TabTreeStore::Result::kOk ||
      node.tombstone || node.type != tab_tree::TreeNodeType::kSavedPage) {
    return false;
  }

  // A closed saved page is opened in the background before its persistent
  // row is removed; dropping it below the separator must never make it
  // disappear instead of becoming a live temporary tab.
  tabs::TabInterface* tab = session_bridge_->FindTabByTreeNodeId(node.id);
  if (!tab) {
    NavigateParams params(browser_, node.url,
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    ::Navigate(&params);
    tab = session_bridge_->FindTabByWebContents(
        params.navigated_or_inserted_contents);
    if (!tab || !session_bridge_->BindTreeNodeToTab(node, tab)) {
      if (tab) {
        tab->Close();
      }
      return false;
    }
  }
  const bool extract_split_pane = tab->IsSplit();
  session_bridge_->MakeTabTemporary(tab);
  const tab_tree::TabTreeStore::Result result =
      controller_->DeleteNode(source_node_id, base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    CHECK(session_bridge_->BindTreeNodeToTab(node, tab));
    OnMutationFailed(result);
    return false;
  }
  if (extract_split_pane) {
    CHECK(ExtractTabFromSplitPreservingRemainder(tab_strip_model_, tab));
  }
  OnSidebarDragStateChanged(std::nullopt);
  ScheduleRuntimePresentationRefresh();
  return true;
}

}  // namespace ahoi::sidebar
