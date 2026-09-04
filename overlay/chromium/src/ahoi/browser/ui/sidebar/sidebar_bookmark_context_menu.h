// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_CONTEXT_MENU_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_CONTEXT_MENU_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu.h"
#include "ui/views/view_tracker.h"

class BookmarkMergedSurfaceService;
class Browser;

namespace ahoi::sidebar {

// The native bookmark actions are shared, but stock bookmark-bar visibility
// and Apps/tab-group shortcuts do not control Ahoi's independent shelf.
void HideStockBookmarkBarOptions(views::MenuItemView* menu);

// Owns the clipboard capability query and native context menu independently of
// the clicked button. Only stable node IDs cross the asynchronous boundary.
class SidebarBookmarkContextMenu final : public BookmarkContextMenuObserver {
 public:
  SidebarBookmarkContextMenu(Browser* browser,
                             BookmarkMergedSurfaceService* service,
                             base::RepeatingClosure closed_callback);
  SidebarBookmarkContextMenu(const SidebarBookmarkContextMenu&) = delete;
  SidebarBookmarkContextMenu& operator=(const SidebarBookmarkContextMenu&) =
      delete;
  ~SidebarBookmarkContextMenu() override;

  void RunAt(views::View* source,
             const std::vector<const bookmarks::BookmarkNode*>& nodes,
             const gfx::Point& point,
             ui::mojom::MenuSourceType source_type);
  bool pending() const { return !menu_; }
  BookmarkContextMenu* native_menu_for_testing() const { return menu_.get(); }

  // BookmarkContextMenuObserver:
  void WillRemoveBookmarks(
      const std::vector<raw_ptr<const bookmarks::BookmarkNode,
                                VectorExperimental>>& nodes) override;
  void DidRemoveBookmarks() override;
  void OnContextMenuClosed() override;

 private:
  void ClipboardChecked(std::vector<int64_t> node_ids,
                        gfx::Point point,
                        ui::mojom::MenuSourceType source_type,
                        bool can_paste);

  const raw_ptr<Browser> browser_;
  const raw_ptr<BookmarkMergedSurfaceService> service_;
  base::WeakPtr<views::Widget> widget_;
  views::ViewTracker source_;
  base::RepeatingClosure closed_callback_;
  std::unique_ptr<BookmarkContextMenu> menu_;
  base::ScopedObservation<BookmarkContextMenu, BookmarkContextMenuObserver>
      observation_{this};
  base::WeakPtrFactory<SidebarBookmarkContextMenu> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_CONTEXT_MENU_H_
