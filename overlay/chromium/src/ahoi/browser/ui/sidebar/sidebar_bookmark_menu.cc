// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_menu.h"

#include <utility>

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_context_menu.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_menu_delegate.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

// Chromium's delegate allocates from AppMenuModel's bookmark range.
// The adapter's one command stays below that range.
constexpr int kOpenAllCommand = 1;

}  // namespace

SidebarBookmarkMenu::SidebarBookmarkMenu(
    Browser* browser,
    BookmarkMergedSurfaceService* bookmark_service,
    const BookmarkParentFolder& folder,
    base::RepeatingClosure closed_callback)
    : browser_(browser),
      bookmark_service_(bookmark_service),
      permanent_type_(folder.as_permanent_folder()),
      closed_callback_(std::move(closed_callback)) {
  CHECK(browser_);
  CHECK(bookmark_service_);
  if (const auto* node = folder.as_non_permanent_folder()) {
    folder_uuid_ = node->uuid();
    is_account_folder_ =
        bookmark_service_->bookmark_model()->GetNodeByUuid(
            folder_uuid_,
            bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes) ==
        node;
  }
}

SidebarBookmarkMenu::~SidebarBookmarkMenu() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  closed_callback_.Reset();
  Cancel();
}

void SidebarBookmarkMenu::RunAt(views::View* anchor, const ui::Event& event) {
  CHECK(anchor);
  CHECK(anchor->GetWidget());
  CHECK(!runner_);
  anchor_.SetView(anchor);
  anchor_widget_ = anchor->GetWidget()->GetWeakPtr();
  const auto folder = ResolveFolder();
  if (!folder) {
    OnMenuClosed(nullptr);
    return;
  }
  delegate_ = std::make_unique<BookmarkMenuDelegate>(
      browser_, anchor->GetWidget(), this, BookmarkLaunchLocation::kSubfolder);
  delegate_->SetContextMenuPresentationCallback(base::BindRepeating(
      [](base::WeakPtr<SidebarBookmarkMenu> owner, views::MenuItemView* menu) {
        return owner && owner->PrepareContextMenu(menu);
      },
      weak_ptr_factory_.GetWeakPtr()));
  delegate_->SetActiveMenu(*folder, 0);
  auto menu = base::WrapUnique(delegate_->menu());
  const auto nodes = bookmark_service_->GetUnderlyingNodes(*folder);
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      selection(nodes.begin(), nodes.end());
  if (bookmarks::HasBookmarkURLs(selection)) {
    menu->AppendSeparator();
    open_all_item_ = menu->AppendMenuItem(
        kOpenAllCommand,
        l10n_util::GetPluralStringFUTF16(IDS_BOOKMARK_BAR_OPEN_ALL_COUNT, 0));
  }
  // Nested content is populated by BookmarkMenuDelegate::WillShowMenu only
  // when opened. Views supplies the standard empty-folder item.
  runner_ = std::make_unique<views::MenuRunner>(std::move(menu),
                                                views::MenuRunner::NO_FLAGS);
  runner_->RunMenuAt(anchor->GetWidget(), /*button_controller=*/nullptr,
                     anchor->GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                                        : ui::mojom::MenuSourceType::kMouse);
}

void SidebarBookmarkMenu::Cancel() {
  cancelled_ = true;
  if (runner_) {
    runner_->Cancel();
  }
  open_all_item_ = nullptr;
  // Release the native delegate's borrowed menu-item references while the
  // runner still owns those views. Cancel has already stopped UI dispatch.
  delegate_.reset();
  runner_.reset();
}

bool SidebarBookmarkMenu::is_mutating_model() const {
  return delegate_ && delegate_->is_mutating_model();
}

views::MenuItemView* SidebarBookmarkMenu::menu_for_testing() const {
  return delegate_ ? delegate_->menu() : nullptr;
}

views::MenuItemView* SidebarBookmarkMenu::context_menu_for_testing() const {
  return delegate_ ? delegate_->context_menu() : nullptr;
}

std::u16string SidebarBookmarkMenu::GetTooltipText(
    int id,
    const gfx::Point& point) const {
  return id == kOpenAllCommand ? std::u16string()
                               : delegate_->GetTooltipText(id, point);
}

bool SidebarBookmarkMenu::IsTriggerableEvent(views::MenuItemView* menu,
                                             const ui::Event& event) {
  return !cancelled_ && delegate_->IsTriggerableEvent(menu, event);
}

bool SidebarBookmarkMenu::IsCommandEnabled(int id) const {
  if (cancelled_ || id != kOpenAllCommand) {
    return !cancelled_;
  }
  const auto folder = ResolveFolder();
  if (!folder) {
    return false;
  }
  const auto nodes = bookmark_service_->GetUnderlyingNodes(*folder);
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      selection(nodes.begin(), nodes.end());
  return bookmarks::HasBookmarkURLs(selection);
}

void SidebarBookmarkMenu::RefreshOpenAllState() {
  if (open_all_item_) {
    open_all_item_->SetEnabled(IsCommandEnabled(kOpenAllCommand));
  }
}

void SidebarBookmarkMenu::ExecuteCommand(int id, int event_flags) {
  if (cancelled_) {
    return;
  }
  if (id != kOpenAllCommand) {
    // Navigating a live saved Ahoi page changes that page's durable URL.
    // A bookmark activation therefore opens a normal foreground tab by
    // default; explicit background-tab/window modifiers retain Chromium's
    // behavior. The native delegate accepts flags, rather than a disposition.
    if (ui::DispositionFromEventFlags(event_flags) ==
        WindowOpenDisposition::CURRENT_TAB) {
      event_flags |= ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN;
    }
    delegate_->ExecuteCommand(id, event_flags);
    return;
  }
  const auto folder = ResolveFolder();
  if (!folder) {
    return;
  }
  const auto nodes = bookmark_service_->GetUnderlyingNodes(*folder);
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      selection(nodes.begin(), nodes.end());
  bookmarks::OpenAllIfAllowed(browser_, selection,
                              WindowOpenDisposition::NEW_BACKGROUND_TAB);
}

bool SidebarBookmarkMenu::ShouldExecuteCommandWithoutClosingMenu(
    int id,
    const ui::Event& event) {
  return id != kOpenAllCommand &&
         delegate_->ShouldExecuteCommandWithoutClosingMenu(id, event);
}

bool SidebarBookmarkMenu::ShowContextMenu(
    views::MenuItemView* source,
    int id,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (cancelled_ || id == kOpenAllCommand) {
    return false;
  }
  return delegate_->ShowContextMenu(source, id, point, source_type);
}

int SidebarBookmarkMenu::GetMaxWidthForMenu(views::MenuItemView* view) {
  return delegate_->GetMaxWidthForMenu(view);
}

void SidebarBookmarkMenu::WillShowMenu(views::MenuItemView* menu) {
  delegate_->WillShowMenu(menu);
}

bool SidebarBookmarkMenu::ShouldTryPositioningBesideAnchor() const {
  return false;
}

void SidebarBookmarkMenu::OnMenuClosed(views::MenuItemView* menu) {
  cancelled_ = true;
  if (closed_callback_) {
    std::exchange(closed_callback_, {}).Run();
  }
}

bool SidebarBookmarkMenu::PrepareContextMenu(views::MenuItemView* menu) {
  if (cancelled_ || !anchor_ || !anchor_widget_ || !anchor_.view()->IsDrawn() ||
      anchor_.view()->GetWidget() != anchor_widget_.get() ||
      anchor_.view()->GetVisibleRect().IsEmpty()) {
    return false;
  }
  HideStockBookmarkBarOptions(menu);
  return true;
}

std::optional<BookmarkParentFolder> SidebarBookmarkMenu::ResolveFolder() const {
  if (permanent_type_) {
    switch (*permanent_type_) {
      case BookmarkParentFolder::PermanentFolderType::kBookmarkBarNode:
        return BookmarkParentFolder::BookmarkBarFolder();
      case BookmarkParentFolder::PermanentFolderType::kOtherNode:
        return BookmarkParentFolder::OtherFolder();
      case BookmarkParentFolder::PermanentFolderType::kMobileNode:
        return BookmarkParentFolder::MobileFolder();
      case BookmarkParentFolder::PermanentFolderType::kManagedNode:
        return BookmarkParentFolder::ManagedFolder();
    }
  }
  const auto* node = bookmark_service_->bookmark_model()->GetNodeByUuid(
      folder_uuid_,
      is_account_folder_
          ? bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
          : bookmarks::BookmarkModel::NodeTypeForUuidLookup::
                kLocalOrSyncableNodes);
  return node && node->is_folder()
             ? std::make_optional(BookmarkParentFolder::FromFolderNode(node))
             : std::nullopt;
}

}  // namespace ahoi::sidebar
