// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/uuid.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "ui/views/controls/menu/menu_delegate.h"

class BookmarkMenuDelegate;
class BookmarkMergedSurfaceService;
class Browser;

namespace views {
class MenuRunner;
}  // namespace views

namespace ahoi::sidebar {

// Adapts Chromium's canonical bookmark menu to a sidebar anchor. Chromium owns
// lazy submenus, URL tooltips, navigation policy and bookmark context actions;
// the shelf owns the runner and cancels it on structural model changes.
class SidebarBookmarkMenu final : public views::MenuDelegate {
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
  bool is_mutating_model() const;

  // views::MenuDelegate:
  std::u16string GetTooltipText(int id, const gfx::Point& p) const override;
  bool IsTriggerableEvent(views::MenuItemView* menu,
                          const ui::Event& event) override;
  bool IsCommandEnabled(int id) const override;
  void ExecuteCommand(int id, int event_flags) override;
  bool ShouldExecuteCommandWithoutClosingMenu(int id,
                                              const ui::Event& event) override;
  bool ShowContextMenu(views::MenuItemView* source,
                       int id,
                       const gfx::Point& p,
                       ui::mojom::MenuSourceType source_type) override;
  int GetMaxWidthForMenu(views::MenuItemView* view) override;
  void WillShowMenu(views::MenuItemView* menu) override;
  bool ShouldTryPositioningBesideAnchor() const override;
  void OnMenuClosed(views::MenuItemView* menu) override;

 private:
  std::optional<BookmarkParentFolder> ResolveFolder() const;

  const raw_ptr<Browser> browser_;
  const raw_ptr<BookmarkMergedSurfaceService> bookmark_service_;
  const std::optional<BookmarkParentFolder::PermanentFolderType>
      permanent_type_;
  base::Uuid folder_uuid_;
  bool is_account_folder_ = false;
  base::RepeatingClosure closed_callback_;
  std::unique_ptr<BookmarkMenuDelegate> delegate_;
  // Destroy the runner first: it disconnects callbacks before the delegate
  // and its bookmark references are released.
  std::unique_ptr<views::MenuRunner> runner_;
  bool cancelled_ = false;
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_BOOKMARK_MENU_H_
