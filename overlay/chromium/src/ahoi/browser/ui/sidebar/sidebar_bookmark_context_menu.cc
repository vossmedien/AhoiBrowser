// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_context_menu.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/ui/bookmarks/bookmark_ui_operations_helper.h"
#include "chrome/browser/ui/browser.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

void HideStockBookmarkBarOptions(views::MenuItemView* menu) {
  if (!menu) {
    return;
  }
  for (int command : {IDC_BOOKMARK_BAR_ALWAYS_SHOW, IDC_BOOKMARK_BAR_SUBMENU,
                      IDC_BOOKMARK_BAR_SHOW_APPS_SHORTCUT,
                      IDC_BOOKMARK_BAR_TOGGLE_SHOW_TAB_GROUPS,
                      IDC_BOOKMARK_BAR_SHOW_MANAGED_BOOKMARKS}) {
    if (auto* item = menu->GetMenuItemByID(command)) {
      item->SetVisible(false);
    }
  }
  menu->ChildrenChanged();
}

SidebarBookmarkContextMenu::SidebarBookmarkContextMenu(
    Browser* browser,
    BookmarkMergedSurfaceService* service,
    base::RepeatingClosure closed_callback)
    : browser_(browser),
      service_(service),
      closed_callback_(std::move(closed_callback)) {}

SidebarBookmarkContextMenu::~SidebarBookmarkContextMenu() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  observation_.Reset();
  menu_.reset();
}

void SidebarBookmarkContextMenu::RunAt(
    views::View* source,
    const std::vector<const bookmarks::BookmarkNode*>& nodes,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!source || !source->GetWidget() || nodes.empty() || !service_->loaded()) {
    OnContextMenuClosed();
    return;
  }
  widget_ = source->GetWidget()->GetWeakPtr();
  source_.SetView(source);
  std::vector<int64_t> ids;
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      selection;
  for (const auto* node : nodes) {
    ids.push_back(node->id());
    selection.push_back(node);
  }
  const auto parent =
      BookmarkContextMenuController::GetParentForNewNodes(selection);
  BookmarkUIOperationsHelperMergedSurfaces(service_, parent.get())
      .CanPasteFromClipboard(base::BindOnce(
          &SidebarBookmarkContextMenu::ClipboardChecked,
          weak_ptr_factory_.GetWeakPtr(), std::move(ids), point, source_type));
}

void SidebarBookmarkContextMenu::ClipboardChecked(
    std::vector<int64_t> node_ids,
    gfx::Point point,
    ui::mojom::MenuSourceType source_type,
    bool can_paste) {
  if (!widget_ || !source_ || !source_.view()->IsDrawn() ||
      source_.view()->GetWidget() != widget_.get() || !service_->loaded()) {
    OnContextMenuClosed();
    return;
  }
  gfx::Rect visible = source_.view()->GetVisibleBounds();
  views::View::ConvertRectToScreen(source_.view(), &visible);
  if (visible.IsEmpty() || (source_type == ui::mojom::MenuSourceType::kMouse &&
                            !visible.Contains(point))) {
    OnContextMenuClosed();
    return;
  }
  if (source_type == ui::mojom::MenuSourceType::kKeyboard) {
    point = visible.CenterPoint();
  }
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      selection;
  for (int64_t id : node_ids) {
    const auto* node =
        bookmarks::GetBookmarkNodeByID(service_->bookmark_model(), id);
    if (!node) {
      OnContextMenuClosed();
      return;
    }
    selection.push_back(node);
  }
  menu_ = std::make_unique<BookmarkContextMenu>(
      widget_.get(), browser_, browser_->GetProfile(),
      BookmarkLaunchLocation::kAttachedBar, selection,
      /*close_on_remove=*/true, can_paste);
  observation_.Observe(menu_.get());
  HideStockBookmarkBarOptions(menu_->menu());
  menu_->RunMenuAt(point, source_type);
}

void SidebarBookmarkContextMenu::WillRemoveBookmarks(
    const std::vector<
        raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>& nodes) {}

void SidebarBookmarkContextMenu::DidRemoveBookmarks() {}

void SidebarBookmarkContextMenu::OnContextMenuClosed() {
  observation_.Reset();
  if (closed_callback_) {
    // The shelf defers destruction until the native command stack unwinds.
    std::exchange(closed_callback_, {}).Run();
  }
}

}  // namespace ahoi::sidebar
