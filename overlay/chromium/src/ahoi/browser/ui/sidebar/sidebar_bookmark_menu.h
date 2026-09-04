// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "ui/base/models/image_model.h"
#include "ui/base/window_open_disposition.h"
#include "ui/menus/simple_menu_model.h"

class BookmarkMergedSurfaceService;
class Browser;

namespace bookmarks {
class BookmarkNode;
}  // namespace bookmarks

namespace ui {
class Event;
}  // namespace ui

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace ahoi::sidebar {

// Owns one native, cascading menu for a bookmark folder. The menu deliberately
// consumes Chromium's merged bookmark surface, so account and local bookmarks
// keep their canonical ordering without coupling the Ahoi shelf to the
// BookmarkBarView implementation.
class SidebarBookmarkMenu final : public ui::SimpleMenuModel::Delegate {
 public:
  SidebarBookmarkMenu(Browser* browser,
                      BookmarkMergedSurfaceService* bookmark_service,
                      const BookmarkParentFolder& folder,
                      base::RepeatingClosure closed_callback);
  SidebarBookmarkMenu(const SidebarBookmarkMenu&) = delete;
  SidebarBookmarkMenu& operator=(const SidebarBookmarkMenu&) = delete;
  ~SidebarBookmarkMenu() override;

  void RunAt(views::View* anchor, const ui::Event& event);
  void Cancel();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  struct BookmarkNodeReference {
    base::Uuid uuid;
    bool is_account_node = false;
  };

  struct BookmarkFolderReference {
    explicit BookmarkFolderReference(
        BookmarkParentFolder::PermanentFolderType permanent_type);
    explicit BookmarkFolderReference(const bookmarks::BookmarkNode* node);
    BookmarkFolderReference(const BookmarkFolderReference&);
    BookmarkFolderReference& operator=(const BookmarkFolderReference&);
    ~BookmarkFolderReference();

    std::optional<BookmarkParentFolder::PermanentFolderType> permanent_type;
    std::optional<BookmarkNodeReference> node;
  };

  int NextCommandId();
  static BookmarkNodeReference ReferenceForNode(
      const bookmarks::BookmarkNode* node);
  size_t PopulateFolder(const BookmarkParentFolder& folder,
                        ui::SimpleMenuModel* menu);
  const bookmarks::BookmarkNode* ResolveNode(
      const BookmarkNodeReference& reference) const;
  std::optional<BookmarkParentFolder> ResolveFolder(
      const BookmarkFolderReference& reference) const;
  ui::ImageModel IconForNode(const bookmarks::BookmarkNode* node) const;
  void OpenNodes(const std::vector<const bookmarks::BookmarkNode*>& nodes,
                 WindowOpenDisposition disposition);
  void NotifyClosed();

  const raw_ptr<Browser> browser_;
  const raw_ptr<BookmarkMergedSurfaceService> bookmark_service_;
  base::RepeatingClosure closed_callback_;
  ui::SimpleMenuModel root_menu_;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>> submenus_;
  std::map<int, BookmarkNodeReference> url_commands_;
  std::map<int, BookmarkFolderReference> open_folder_commands_;
  std::set<int> disabled_commands_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  int next_command_id_ = 1;
  bool close_notified_ = false;
  base::WeakPtrFactory<SidebarBookmarkMenu> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_
