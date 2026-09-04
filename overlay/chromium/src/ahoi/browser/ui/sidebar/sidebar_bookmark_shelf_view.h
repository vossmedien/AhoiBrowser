// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SHELF_VIEW_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SHELF_VIEW_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_observer.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "ui/base/models/image_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

class BookmarkMergedSurfaceService;
class Browser;

namespace bookmarks {
class BookmarkNode;
}  // namespace bookmarks

namespace ui {
class Event;
}  // namespace ui

namespace views {
class Button;
class Label;
class ScrollView;
}  // namespace views

namespace ahoi::sidebar {

class SidebarBookmarkMenu;
class SidebarBookmarkButton;
class SidebarBookmarkContextMenu;

// A compact bookmark shelf that stays above the independently scrolling saved
// and temporary tab surfaces. URL items and folders have labelled buttons,
// and the strip itself scrolls horizontally without exposing
// a second scrollbar in the narrow sidebar.
class SidebarBookmarkShelfView final
    : public views::View,
      public BookmarkMergedSurfaceServiceObserver,
      public views::ContextMenuController {
  METADATA_HEADER(SidebarBookmarkShelfView, views::View)

 public:
  explicit SidebarBookmarkShelfView(Browser* browser);
  SidebarBookmarkShelfView(const SidebarBookmarkShelfView&) = delete;
  SidebarBookmarkShelfView& operator=(const SidebarBookmarkShelfView&) = delete;
  ~SidebarBookmarkShelfView() override;

  size_t bookmark_item_count_for_testing() const;
  views::View* bookmark_item_at_for_testing(size_t index) const;
  views::Button* manager_button_for_testing() const;
  views::ScrollView* scroll_view_for_testing() const;
  SidebarBookmarkMenu* folder_menu_for_testing() const {
    return folder_menu_.get();
  }
  views::View* leading_overflow_for_testing() const {
    return leading_overflow_;
  }
  views::View* trailing_overflow_for_testing() const {
    return trailing_overflow_;
  }
  SidebarBookmarkContextMenu* context_menu_for_testing() const {
    return context_menu_.get();
  }

  // BookmarkMergedSurfaceServiceObserver:
  void BookmarkMergedSurfaceServiceLoaded() override;
  void BookmarkMergedSurfaceServiceBeingDeleted() override;
  void BookmarkNodeAdded(const BookmarkParentFolder& parent,
                         size_t index) override;
  void BookmarkNodesRemoved(
      const BookmarkParentFolder& parent,
      const base::flat_set<const bookmarks::BookmarkNode*>& nodes) override;
  void BookmarkNodeMoved(const BookmarkParentFolder& old_parent,
                         size_t old_index,
                         const BookmarkParentFolder& new_parent,
                         size_t new_index) override;
  void BookmarkNodeChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkNodeFaviconChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkParentFolderChildrenReordered(
      const BookmarkParentFolder& folder) override;
  void BookmarkAllUserNodesRemoved() override;
  void ExtensiveBookmarkChangesBeginning() override;
  void ExtensiveBookmarkChangesEnded() override;

 private:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;
  void ContextMenuClosed(size_t generation);
  void ResetContextMenu(size_t generation);
  void RevealPendingFocusedBookmark(size_t generation);
  struct BookmarkNodeReference {
    base::Uuid uuid;
    bool is_account_node = false;
  };

  void Rebuild();
  void ScheduleRebuild();
  void ModelChanged();
  void AddBookmarkButton(const bookmarks::BookmarkNode* node);
  void AddPermanentFolderButton(BookmarkParentFolder::PermanentFolderType type,
                                int title_string_id);
  std::string KeyForNode(const bookmarks::BookmarkNode* node) const;
  ui::ImageModel IconForNode(const bookmarks::BookmarkNode* node) const;
  const bookmarks::BookmarkNode* ResolveNode(
      const BookmarkNodeReference& reference) const;
  void OnBookmarkPressed(BookmarkNodeReference reference,
                         views::View* anchor,
                         const ui::Event& event);
  void OnPermanentFolderPressed(BookmarkParentFolder::PermanentFolderType type,
                                views::View* anchor,
                                const ui::Event& event);
  void OpenFolder(const BookmarkParentFolder& folder, int event_flags);
  void ShowFolderMenu(const BookmarkParentFolder& folder,
                      views::View* anchor,
                      const ui::Event& event);
  void OnFolderMenuClosed(size_t generation);
  void ResetClosedMenu(size_t generation);
  void OpenBookmarkManager(const ui::Event& event);

  const raw_ptr<Browser> browser_;
  raw_ptr<BookmarkMergedSurfaceService> bookmark_service_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> leading_overflow_ = nullptr;
  raw_ptr<views::View> trailing_overflow_ = nullptr;
  raw_ptr<views::View> bookmark_items_ = nullptr;
  raw_ptr<views::Button> manager_button_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  std::map<std::string, raw_ptr<SidebarBookmarkButton>> buttons_;
  std::set<std::string> desired_keys_;
  std::unique_ptr<SidebarBookmarkMenu> folder_menu_;
  std::unique_ptr<SidebarBookmarkContextMenu> context_menu_;
  size_t context_menu_generation_ = 0;
  bool rebuild_after_context_closes_ = false;
  std::optional<std::string> pending_focus_reveal_key_;
  size_t focus_reveal_generation_ = 0;
  views::ViewTracker folder_menu_anchor_;
  size_t bookmark_item_count_ = 0;
  size_t folder_menu_generation_ = 0;
  bool rebuild_scheduled_ = false;
  bool rebuild_after_menu_closes_ = false;
  bool extensive_changes_ongoing_ = false;
  bool rebuild_after_extensive_changes_ = false;
  base::WeakPtrFactory<SidebarBookmarkShelfView> weak_ptr_factory_{this};
};

std::unique_ptr<views::View> CreateSidebarBookmarkShelfView(Browser* browser);

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_SHELF_VIEW_H_
