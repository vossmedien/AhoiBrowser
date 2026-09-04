// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_menu.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder_children.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::sidebar {

namespace {

using PermanentFolderType = BookmarkParentFolder::PermanentFolderType;

BookmarkParentFolder PermanentFolder(PermanentFolderType type) {
  switch (type) {
    case PermanentFolderType::kBookmarkBarNode:
      return BookmarkParentFolder::BookmarkBarFolder();
    case PermanentFolderType::kOtherNode:
      return BookmarkParentFolder::OtherFolder();
    case PermanentFolderType::kMobileNode:
      return BookmarkParentFolder::MobileFolder();
    case PermanentFolderType::kManagedNode:
      return BookmarkParentFolder::ManagedFolder();
  }
  NOTREACHED();
}

}  // namespace

SidebarBookmarkMenu::BookmarkFolderReference::BookmarkFolderReference(
    PermanentFolderType type)
    : permanent_type(type) {}

SidebarBookmarkMenu::BookmarkFolderReference::BookmarkFolderReference(
    const bookmarks::BookmarkNode* bookmark_node)
    : node(ReferenceForNode(bookmark_node)) {}

SidebarBookmarkMenu::BookmarkFolderReference::BookmarkFolderReference(
    const BookmarkFolderReference&) = default;

SidebarBookmarkMenu::BookmarkFolderReference&
SidebarBookmarkMenu::BookmarkFolderReference::operator=(
    const BookmarkFolderReference&) = default;

SidebarBookmarkMenu::BookmarkFolderReference::~BookmarkFolderReference() =
    default;

SidebarBookmarkMenu::SidebarBookmarkMenu(
    Browser* browser,
    BookmarkMergedSurfaceService* bookmark_service,
    const BookmarkParentFolder& folder,
    base::RepeatingClosure closed_callback)
    : browser_(browser),
      bookmark_service_(bookmark_service),
      closed_callback_(std::move(closed_callback)),
      root_menu_(this) {
  CHECK(browser_);
  CHECK(bookmark_service_);
  PopulateFolder(folder, &root_menu_);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      &root_menu_, views::MenuRunner::NO_FLAGS,
      base::BindRepeating(&SidebarBookmarkMenu::NotifyClosed,
                          weak_ptr_factory_.GetWeakPtr()));
}

SidebarBookmarkMenu::~SidebarBookmarkMenu() = default;

void SidebarBookmarkMenu::RunAt(views::View* anchor, const ui::Event& event) {
  CHECK(anchor);
  CHECK(anchor->GetWidget());
  const ui::mojom::MenuSourceType source_type =
      event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                         : ui::mojom::MenuSourceType::kMouse;
  menu_runner_->RunMenuAt(anchor->GetWidget(), /*button_controller=*/nullptr,
                          anchor->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void SidebarBookmarkMenu::Cancel() {
  if (menu_runner_) {
    menu_runner_->Cancel();
  }
}

bool SidebarBookmarkMenu::IsCommandIdEnabled(int command_id) const {
  if (disabled_commands_.contains(command_id)) {
    return false;
  }
  const auto node = url_commands_.find(command_id);
  if (node != url_commands_.end()) {
    return ResolveNode(node->second) != nullptr;
  }
  const auto folder = open_folder_commands_.find(command_id);
  return folder == open_folder_commands_.end() ||
         ResolveFolder(folder->second).has_value();
}

void SidebarBookmarkMenu::ExecuteCommand(int command_id, int event_flags) {
  const auto url = url_commands_.find(command_id);
  if (url != url_commands_.end()) {
    if (const auto* node = ResolveNode(url->second)) {
      OpenNodes({node}, ui::DispositionFromEventFlags(event_flags));
    }
    return;
  }

  const auto folder = open_folder_commands_.find(command_id);
  if (folder == open_folder_commands_.end()) {
    return;
  }
  const auto resolved_folder = ResolveFolder(folder->second);
  if (!resolved_folder) {
    return;
  }
  OpenNodes(bookmark_service_->GetUnderlyingNodes(*resolved_folder),
            WindowOpenDisposition::NEW_BACKGROUND_TAB);
}

int SidebarBookmarkMenu::NextCommandId() {
  return next_command_id_++;
}

SidebarBookmarkMenu::BookmarkNodeReference
SidebarBookmarkMenu::ReferenceForNode(
    const bookmarks::BookmarkNode* node) const {
  const bool is_account_node =
      bookmark_service_->bookmark_model()->GetNodeByUuid(
          node->uuid(),
          bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes) ==
      node;
  return {.uuid = node->uuid(), .is_account_node = is_account_node};
}

size_t SidebarBookmarkMenu::PopulateFolder(const BookmarkParentFolder& folder,
                                           ui::SimpleMenuModel* menu) {
  size_t url_count = 0;
  const BookmarkParentFolderChildren children =
      bookmark_service_->GetChildren(folder);
  for (const bookmarks::BookmarkNode* node : children) {
    const int command_id = NextCommandId();
    if (node->is_url()) {
      url_commands_.emplace(command_id, ReferenceForNode(node));
      menu->AddItemWithIcon(command_id, node->GetTitle(), IconForNode(node));
      ++url_count;
      continue;
    }

    auto submenu = std::make_unique<ui::SimpleMenuModel>(this);
    ui::SimpleMenuModel* const submenu_ptr = submenu.get();
    submenus_.push_back(std::move(submenu));
    url_count +=
        PopulateFolder(BookmarkParentFolder::FromFolderNode(node), submenu_ptr);
    menu->AddSubMenuWithIcon(
        command_id, node->GetTitle(), submenu_ptr,
        ui::ImageModel::FromVectorIcon(vector_icons::kFolderFlippableIcon,
                                       ui::kColorMenuIcon, 16));
  }

  if (children.size() == 0) {
    const int empty_command_id = NextCommandId();
    disabled_commands_.insert(empty_command_id);
    menu->AddItem(
        empty_command_id,
        l10n_util::GetStringUTF16(IDS_BOOKMARKS_EMPTY_STATE_TITLE_FOLDER));
  } else if (url_count > 0) {
    menu->AddSeparator(ui::NORMAL_SEPARATOR);
    const int open_all_command_id = NextCommandId();
    const auto permanent_type = folder.as_permanent_folder();
    if (permanent_type) {
      open_folder_commands_.emplace(open_all_command_id,
                                    BookmarkFolderReference(*permanent_type));
    } else {
      open_folder_commands_.emplace(
          open_all_command_id,
          BookmarkFolderReference(folder.as_non_permanent_folder()));
    }
    menu->AddItem(open_all_command_id,
                  l10n_util::GetPluralStringFUTF16(
                      IDS_BOOKMARK_BAR_OPEN_ALL_COUNT, url_count));
  }
  return url_count;
}

const bookmarks::BookmarkNode* SidebarBookmarkMenu::ResolveNode(
    const BookmarkNodeReference& reference) const {
  const auto lookup_type =
      reference.is_account_node
          ? bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
          : bookmarks::BookmarkModel::NodeTypeForUuidLookup::
                kLocalOrSyncableNodes;
  return bookmark_service_->bookmark_model()->GetNodeByUuid(reference.uuid,
                                                            lookup_type);
}

std::optional<BookmarkParentFolder> SidebarBookmarkMenu::ResolveFolder(
    const BookmarkFolderReference& reference) const {
  if (reference.permanent_type) {
    return PermanentFolder(*reference.permanent_type);
  }
  if (!reference.node) {
    return std::nullopt;
  }
  const bookmarks::BookmarkNode* const node = ResolveNode(*reference.node);
  if (!node || !node->is_folder()) {
    return std::nullopt;
  }
  return BookmarkParentFolder::FromFolderNode(node);
}

ui::ImageModel SidebarBookmarkMenu::IconForNode(
    const bookmarks::BookmarkNode* node) const {
  const ui::ImageModel favicon = ui::ImageModel::FromImage(
      bookmark_service_->bookmark_model()->GetFavicon(node));
  if (!favicon.IsEmpty()) {
    return favicon;
  }
  return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                        ui::kColorMenuIcon, 16);
}

void SidebarBookmarkMenu::OpenNodes(
    const std::vector<const bookmarks::BookmarkNode*>& nodes,
    WindowOpenDisposition disposition) {
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>
      raw_nodes;
  raw_nodes.reserve(nodes.size());
  for (const bookmarks::BookmarkNode* node : nodes) {
    raw_nodes.emplace_back(node);
  }
  bookmarks::OpenAllIfAllowed(browser_, raw_nodes, disposition);
}

void SidebarBookmarkMenu::NotifyClosed() {
  if (close_notified_) {
    return;
  }
  close_notified_ = true;
  if (closed_callback_) {
    closed_callback_.Run();
  }
}

}  // namespace ahoi::sidebar
