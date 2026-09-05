// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_bookmark_shelf_view.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/sidebar/browser_sidebar_host.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_button.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_context_menu.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_menu.h"
#include "ahoi/browser/ui/sidebar/sidebar_bookmark_sync_control.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder_children.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"

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

ui::ImageModel FolderIcon(ui::ColorId color) {
  return ui::ImageModel::FromVectorIcon(vector_icons::kFolderFlippableIcon,
                                        color, visual_style::kSidebarIconSize);
}

std::unique_ptr<views::View> OverflowArrow(const gfx::VectorIcon& icon) {
  auto view = std::make_unique<views::ImageView>();
  view->SetImage(
      ui::ImageModel::FromVectorIcon(icon, visual_style::kMutedText, 12));
  view->SetBackground(
      views::CreateSolidBackground(visual_style::kRaisedSurface));
  view->SetCanProcessEventsWithinSubtree(false);
  view->GetViewAccessibility().SetIsIgnored(true);
  return view;
}

}  // namespace

SidebarBookmarkShelfView::SidebarBookmarkShelfView(Browser* browser)
    : browser_(browser) {
  CHECK(browser_);
  set_context_menu_controller(this);
  SetPreferredSize(gfx::Size(0, visual_style::kBookmarkShelfHeight));
  SetBackground(nullptr);
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(visual_style::kBookmarkShelfVerticalInset, 0)));
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_BOOKMARKS));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      visual_style::kBookmarkShelfSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto items = std::make_unique<views::View>();
  bookmark_items_ = items.get();
  auto* items_layout =
      bookmark_items_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          visual_style::kBookmarkShelfSpacing));
  items_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  bookmark_items_->GetViewAccessibility().SetIsIgnored(true);

  auto scroll = std::make_unique<views::ScrollView>();
  scroll_view_ = scroll.get();
  scroll_view_->SetUseContentsPreferredSize(true);
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetDrawOverflowIndicator(true);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetTreatAllScrollEventsAsHorizontal(true);
  scroll_view_->SetContents(std::move(items));
  leading_overflow_ = scroll_view_->SetCustomOverflowIndicator(
      views::OverflowIndicatorAlignment::kLeft,
      OverflowArrow(vector_icons::kArrowBackIcon), 14,
      /*fills_opaquely=*/false);
  trailing_overflow_ = scroll_view_->SetCustomOverflowIndicator(
      views::OverflowIndicatorAlignment::kRight,
      OverflowArrow(vector_icons::kArrowForwardIcon), 14,
      /*fills_opaquely=*/false);
  scroll_view_->RegisterPostLayoutCallback(base::BindRepeating(
      [](base::WeakPtr<SidebarBookmarkShelfView> shelf,
         views::ScrollView* scroll) {
        if (shelf && !scroll->contents()->layer()) {
          shelf->RevealPendingFocusedBookmark(shelf->focus_reveal_generation_);
        }
      },
      weak_ptr_factory_.GetWeakPtr()));
  views::View* const scroll_ptr = AddChildView(std::move(scroll));
  layout->SetFlexForView(scroll_ptr, 1);
  auto empty_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_ACCNAME_BOOKMARKS));
  empty_label_ = empty_label.get();
  empty_label_->SetEnabledColor(visual_style::kMutedText);
  empty_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  layout->SetFlexForView(AddChildView(std::move(empty_label)), 1);
  scroll_view_->SetVisible(false);

  auto manager = std::make_unique<SidebarBookmarkButton>(
      base::BindRepeating(&SidebarBookmarkShelfView::OpenBookmarkManager,
                          weak_ptr_factory_.GetWeakPtr()),
      std::u16string(),
      ui::ImageModel::FromVectorIcon(kBookmarkManagerIcon,
                                     visual_style::kMutedText,
                                     visual_style::kSidebarIconSize),
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_V2),
      /*folder=*/false);
  manager_button_ = manager.get();
  AddChildView(std::move(manager));
  if (auto sync_control = CreateBookmarkSyncControl(browser_->GetProfile())) {
    AddChildView(std::move(sync_control));
  }

  bookmark_service_ = BookmarkMergedSurfaceServiceFactory::GetForProfile(
      browser_->GetProfile());
  if (!bookmark_service_) {
    return;
  }
  bookmark_service_->AddObserver(this);
  if (bookmark_service_->loaded()) {
    Rebuild();
  }
}

SidebarBookmarkShelfView::~SidebarBookmarkShelfView() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  context_menu_.reset();
  folder_menu_.reset();
  if (bookmark_service_) {
    bookmark_service_->RemoveObserver(this);
  }
}

size_t SidebarBookmarkShelfView::bookmark_item_count_for_testing() const {
  return bookmark_item_count_;
}

views::View* SidebarBookmarkShelfView::bookmark_item_at_for_testing(
    size_t index) const {
  return index < bookmark_items_->children().size()
             ? bookmark_items_->children()[index]
             : nullptr;
}

views::Button* SidebarBookmarkShelfView::manager_button_for_testing() const {
  return manager_button_;
}

views::ScrollView* SidebarBookmarkShelfView::scroll_view_for_testing() const {
  return scroll_view_;
}

void SidebarBookmarkShelfView::BookmarkMergedSurfaceServiceLoaded() {
  ScheduleRebuild();
}

void SidebarBookmarkShelfView::BookmarkMergedSurfaceServiceBeingDeleted() {
  ++focus_reveal_generation_;
  pending_focus_reveal_key_.reset();
  ++context_menu_generation_;
  context_menu_.reset();
  ++folder_menu_generation_;
  folder_menu_.reset();
  bookmark_service_ = nullptr;
  buttons_.clear();
  bookmark_items_->RemoveAllChildViews();
  bookmark_item_count_ = 0;
  scroll_view_->SetVisible(false);
  empty_label_->SetVisible(true);
}

void SidebarBookmarkShelfView::BookmarkNodeAdded(
    const BookmarkParentFolder& parent,
    size_t index) {
  ModelChanged();
}

void SidebarBookmarkShelfView::BookmarkNodesRemoved(
    const BookmarkParentFolder& parent,
    const base::flat_set<const bookmarks::BookmarkNode*>& nodes) {
  ModelChanged();
}

void SidebarBookmarkShelfView::BookmarkNodeMoved(
    const BookmarkParentFolder& old_parent,
    size_t old_index,
    const BookmarkParentFolder& new_parent,
    size_t new_index) {
  ModelChanged();
}

void SidebarBookmarkShelfView::BookmarkNodeChanged(
    const bookmarks::BookmarkNode* node) {
  ModelChanged();
}

void SidebarBookmarkShelfView::BookmarkNodeFaviconChanged(
    const bookmarks::BookmarkNode* node) {
  const auto found = buttons_.find(KeyForNode(node));
  if (found == buttons_.end()) {
    return;
  }
  const ui::ImageModel icon = IconForNode(node);
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
        views::Button::STATE_PRESSED}) {
    found->second->SetImageModel(state, icon);
  }
}

void SidebarBookmarkShelfView::BookmarkParentFolderChildrenReordered(
    const BookmarkParentFolder& folder) {
  ModelChanged();
}

void SidebarBookmarkShelfView::BookmarkAllUserNodesRemoved() {
  ModelChanged();
}

void SidebarBookmarkShelfView::ExtensiveBookmarkChangesBeginning() {
  extensive_changes_ongoing_ = true;
}

void SidebarBookmarkShelfView::ExtensiveBookmarkChangesEnded() {
  extensive_changes_ongoing_ = false;
  if (rebuild_after_extensive_changes_) {
    rebuild_after_extensive_changes_ = false;
    ScheduleRebuild();
  }
}

void SidebarBookmarkShelfView::Rebuild() {
  rebuild_scheduled_ = false;
  ++focus_reveal_generation_;
  if (!bookmark_service_ || !bookmark_service_->loaded()) {
    return;
  }
  ++folder_menu_generation_;
  folder_menu_.reset();
  if (folder_menu_anchor_) {
    static_cast<SidebarBookmarkButton*>(folder_menu_anchor_.view())
        ->SetMenuOpen(false);
  }
  folder_menu_anchor_.SetView(nullptr);
  std::optional<size_t> focused_index;
  for (size_t i = 0; i < bookmark_items_->children().size(); ++i) {
    if (bookmark_items_->children()[i]->HasFocus()) {
      focused_index = i;
      break;
    }
  }
  desired_keys_.clear();
  bookmark_item_count_ = 0;

  const BookmarkParentFolder bookmark_bar =
      BookmarkParentFolder::BookmarkBarFolder();
  for (const bookmarks::BookmarkNode* node :
       bookmark_service_->GetChildren(bookmark_bar)) {
    AddBookmarkButton(node);
  }
  AddPermanentFolderButton(PermanentFolderType::kOtherNode,
                           IDS_BOOKMARK_BAR_OTHER_FOLDER_NAME);
  AddPermanentFolderButton(PermanentFolderType::kMobileNode,
                           IDS_BOOKMARK_BAR_MOBILE_FOLDER_NAME);
  AddPermanentFolderButton(PermanentFolderType::kManagedNode,
                           IDS_BOOKMARK_BAR_MANAGED_FOLDER_DEFAULT_NAME);
  for (auto it = buttons_.begin(); it != buttons_.end();) {
    if (desired_keys_.contains(it->first)) {
      ++it;
    } else {
      auto* obsolete = it->second.get();
      it = buttons_.erase(it);
      bookmark_items_->RemoveChildViewT(obsolete);
    }
  }
  scroll_view_->SetVisible(bookmark_item_count_ != 0);
  empty_label_->SetVisible(bookmark_item_count_ == 0);
  bookmark_items_->InvalidateLayout();
  scroll_view_->InvalidateLayout();
  if (focused_index && GetFocusManager() &&
      !GetFocusManager()->GetFocusedView()) {
    if (bookmark_item_count_ == 0) {
      manager_button_->RequestFocus();
    } else {
      bookmark_items_
          ->children()[std::min(*focused_index, bookmark_item_count_ - 1)]
          ->RequestFocus();
    }
  }
  pending_focus_reveal_key_.reset();
  for (const auto& [key, button] : buttons_) {
    if (button->HasFocus()) {
      pending_focus_reveal_key_ = key;
      break;
    }
  }
  if (pending_focus_reveal_key_ && scroll_view_->contents()->layer()) {
    scroll_view_->RegisterNextSuccessfulFramePostLayoutCallback(base::BindOnce(
        &SidebarBookmarkShelfView::RevealPendingFocusedBookmark,
        weak_ptr_factory_.GetWeakPtr(), focus_reveal_generation_));
  }
}

void SidebarBookmarkShelfView::RevealPendingFocusedBookmark(size_t generation) {
  if (generation != focus_reveal_generation_) {
    return;
  }
  const auto key = std::exchange(pending_focus_reveal_key_, std::nullopt);
  if (!key) {
    return;
  }
  const auto found = buttons_.find(*key);
  if (found != buttons_.end() && found->second->HasFocus()) {
    found->second->ScrollRectToVisible(found->second->GetLocalBounds());
  }
}

void SidebarBookmarkShelfView::ScheduleRebuild() {
  if (extensive_changes_ongoing_) {
    rebuild_after_extensive_changes_ = true;
    return;
  }
  if (rebuild_scheduled_) {
    return;
  }
  rebuild_scheduled_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarBookmarkShelfView::Rebuild,
                                weak_ptr_factory_.GetWeakPtr()));
}

void SidebarBookmarkShelfView::ModelChanged() {
  if (context_menu_) {
    if (context_menu_->pending()) {
      ++context_menu_generation_;
      context_menu_.reset();
    } else {
      rebuild_after_context_closes_ = true;
      return;
    }
  }
  if (folder_menu_) {
    if (folder_menu_->is_mutating_model()) {
      folder_menu_->RefreshOpenAllState();
      // A native bookmark context action may remove an item inside its own
      // callback. Chromium updates that menu; keep its anchor alive until
      // close.
      rebuild_after_menu_closes_ = true;
      return;
    }
    folder_menu_->Cancel();
  }
  ScheduleRebuild();
}

void SidebarBookmarkShelfView::AddBookmarkButton(
    const bookmarks::BookmarkNode* node) {
  CHECK(node);
  const bool is_account_node =
      bookmark_service_->bookmark_model()->GetNodeByUuid(
          node->uuid(),
          bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes) ==
      node;
  const BookmarkNodeReference reference{.uuid = node->uuid(),
                                        .is_account_node = is_account_node};
  const ui::ImageModel icon = IconForNode(node);
  const std::u16string title =
      !node->GetTitle().empty() ? node->GetTitle()
      : node->is_url() ? base::UTF8ToUTF16(node->url().spec())
                       : l10n_util::GetStringUTF16(IDS_UNNAMED_BOOKMARK_FOLDER);
  const std::u16string tooltip =
      node->is_url() ? title + u"\n" + base::UTF8ToUTF16(node->url().spec())
                     : title;
  const std::string key = KeyForNode(node);
  desired_keys_.insert(key);
  const auto existing = buttons_.find(key);
  if (existing != buttons_.end()) {
    existing->second->UpdatePresentation(title, icon, tooltip);
    bookmark_items_->ReorderChildView(existing->second, bookmark_item_count_++);
    return;
  }
  auto button = std::make_unique<SidebarBookmarkButton>(
      base::BindRepeating([](const ui::Event&) {}), title, icon, tooltip,
      node->is_folder());
  views::View* const anchor = button.get();
  button->SetCallback(
      base::BindRepeating(&SidebarBookmarkShelfView::OnBookmarkPressed,
                          weak_ptr_factory_.GetWeakPtr(), reference, anchor));
  auto* added = bookmark_items_->AddChildView(std::move(button));
  added->set_context_menu_controller(this);
  buttons_.emplace(key, added);
  bookmark_items_->ReorderChildView(added, bookmark_item_count_++);
}

void SidebarBookmarkShelfView::AddPermanentFolderButton(
    PermanentFolderType type,
    int title_string_id) {
  const BookmarkParentFolder folder = PermanentFolder(type);
  if (bookmark_service_->GetChildrenCount(folder) == 0) {
    return;
  }
  const std::u16string title = l10n_util::GetStringUTF16(title_string_id);
  const std::string key =
      "permanent/" + base::NumberToString(static_cast<int>(type));
  desired_keys_.insert(key);
  const auto existing = buttons_.find(key);
  if (existing != buttons_.end()) {
    bookmark_items_->ReorderChildView(existing->second, bookmark_item_count_++);
    return;
  }
  auto button = std::make_unique<SidebarBookmarkButton>(
      base::BindRepeating([](const ui::Event&) {}), title,
      FolderIcon(visual_style::kMutedText), title, /*folder=*/true);
  views::View* const anchor = button.get();
  button->SetCallback(
      base::BindRepeating(&SidebarBookmarkShelfView::OnPermanentFolderPressed,
                          weak_ptr_factory_.GetWeakPtr(), type, anchor));
  auto* added = bookmark_items_->AddChildView(std::move(button));
  added->set_context_menu_controller(this);
  buttons_.emplace(key, added);
  bookmark_items_->ReorderChildView(added, bookmark_item_count_++);
}

std::string SidebarBookmarkShelfView::KeyForNode(
    const bookmarks::BookmarkNode* node) const {
  const bool account =
      bookmark_service_->bookmark_model()->GetNodeByUuid(
          node->uuid(),
          bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes) ==
      node;
  return node->uuid().AsLowercaseString() + (account ? "/account" : "/local");
}

ui::ImageModel SidebarBookmarkShelfView::IconForNode(
    const bookmarks::BookmarkNode* node) const {
  if (node->is_folder()) {
    return FolderIcon(visual_style::kMutedText);
  }
  const ui::ImageModel favicon = ui::ImageModel::FromImage(
      bookmark_service_->bookmark_model()->GetFavicon(node));
  return favicon.IsEmpty()
             ? ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                              visual_style::kMutedText,
                                              visual_style::kSidebarIconSize)
             : favicon;
}

const bookmarks::BookmarkNode* SidebarBookmarkShelfView::ResolveNode(
    const BookmarkNodeReference& reference) const {
  if (!bookmark_service_) {
    return nullptr;
  }
  const auto lookup_type =
      reference.is_account_node
          ? bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
          : bookmarks::BookmarkModel::NodeTypeForUuidLookup::
                kLocalOrSyncableNodes;
  return bookmark_service_->bookmark_model()->GetNodeByUuid(reference.uuid,
                                                            lookup_type);
}

void SidebarBookmarkShelfView::OnBookmarkPressed(
    BookmarkNodeReference reference,
    views::View* anchor,
    const ui::Event& event) {
  const bookmarks::BookmarkNode* const node = ResolveNode(reference);
  if (!node) {
    ScheduleRebuild();
    return;
  }
  if (node->is_url()) {
    const auto disposition = ui::DispositionFromEventFlags(event.flags());
    bookmarks::OpenAllIfAllowed(
        browser_, {node},
        disposition == WindowOpenDisposition::CURRENT_TAB
            ? WindowOpenDisposition::NEW_FOREGROUND_TAB
            : disposition);
    return;
  }
  const BookmarkParentFolder folder =
      BookmarkParentFolder::FromFolderNode(node);
  if ((event.flags() & ui::EF_MIDDLE_MOUSE_BUTTON) ||
      (event.flags() & ui::EF_PLATFORM_ACCELERATOR)) {
    OpenFolder(folder, event.flags());
  } else {
    ShowFolderMenu(folder, anchor, event);
  }
}

void SidebarBookmarkShelfView::OnPermanentFolderPressed(
    PermanentFolderType type,
    views::View* anchor,
    const ui::Event& event) {
  const BookmarkParentFolder folder = PermanentFolder(type);
  if ((event.flags() & ui::EF_MIDDLE_MOUSE_BUTTON) ||
      (event.flags() & ui::EF_PLATFORM_ACCELERATOR)) {
    OpenFolder(folder, event.flags());
  } else {
    ShowFolderMenu(folder, anchor, event);
  }
}

void SidebarBookmarkShelfView::OpenFolder(const BookmarkParentFolder& folder,
                                          int event_flags) {
  std::vector<raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>> nodes;
  for (const bookmarks::BookmarkNode* node :
       bookmark_service_->GetUnderlyingNodes(folder)) {
    nodes.emplace_back(node);
  }
  bookmarks::OpenAllIfAllowed(browser_, nodes,
                              ui::DispositionFromEventFlags(event_flags));
}

void SidebarBookmarkShelfView::ShowFolderMenu(
    const BookmarkParentFolder& folder,
    views::View* anchor,
    const ui::Event& event) {
  if (!bookmark_service_ || !anchor || !anchor->GetWidget()) {
    return;
  }
  ++context_menu_generation_;
  context_menu_.reset();
  const size_t generation = ++folder_menu_generation_;
  if (folder_menu_) {
    if (folder_menu_anchor_) {
      static_cast<SidebarBookmarkButton*>(folder_menu_anchor_.view())
          ->SetMenuOpen(false);
      folder_menu_anchor_.SetView(nullptr);
    }
    folder_menu_->Cancel();
    folder_menu_.reset();
  }
  folder_menu_anchor_.SetView(anchor);
  static_cast<SidebarBookmarkButton*>(anchor)->SetMenuOpen(true);
  folder_menu_ = std::make_unique<SidebarBookmarkMenu>(
      browser_, bookmark_service_, folder,
      base::BindRepeating(&SidebarBookmarkShelfView::OnFolderMenuClosed,
                          weak_ptr_factory_.GetWeakPtr(), generation));
  folder_menu_->RunAt(anchor, event);
}

void SidebarBookmarkShelfView::OnFolderMenuClosed(size_t generation) {
  if (generation != folder_menu_generation_) {
    return;
  }
  if (folder_menu_anchor_) {
    static_cast<SidebarBookmarkButton*>(folder_menu_anchor_.view())
        ->SetMenuOpen(false);
  }
  folder_menu_anchor_.SetView(nullptr);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarBookmarkShelfView::ResetClosedMenu,
                                weak_ptr_factory_.GetWeakPtr(), generation));
}

void SidebarBookmarkShelfView::ResetClosedMenu(size_t generation) {
  if (generation != folder_menu_generation_) {
    return;
  }
  folder_menu_.reset();
  if (rebuild_after_menu_closes_) {
    rebuild_after_menu_closes_ = false;
    ScheduleRebuild();
  }
}

void SidebarBookmarkShelfView::OpenBookmarkManager(const ui::Event& event) {
  chrome::ShowBookmarkManager(browser_);
}

void SidebarBookmarkShelfView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!bookmark_service_ || !bookmark_service_->loaded() || !GetWidget()) {
    return;
  }
  std::vector<const bookmarks::BookmarkNode*> selection;
  if (source == this) {
    selection = bookmark_service_->GetUnderlyingNodes(
        BookmarkParentFolder::BookmarkBarFolder());
  } else {
    for (const auto* node : bookmark_service_->GetChildren(
             BookmarkParentFolder::BookmarkBarFolder())) {
      const auto found = buttons_.find(KeyForNode(node));
      if (found != buttons_.end() && found->second == source) {
        selection.push_back(node);
        break;
      }
    }
    for (auto type :
         {PermanentFolderType::kOtherNode, PermanentFolderType::kMobileNode,
          PermanentFolderType::kManagedNode}) {
      const auto found = buttons_.find(
          "permanent/" + base::NumberToString(static_cast<int>(type)));
      if (found != buttons_.end() && found->second == source) {
        selection =
            bookmark_service_->GetUnderlyingNodes(PermanentFolder(type));
        break;
      }
    }
  }
  if (selection.empty()) {
    return;
  }
  ++folder_menu_generation_;
  folder_menu_.reset();
  if (folder_menu_anchor_) {
    static_cast<SidebarBookmarkButton*>(folder_menu_anchor_.view())
        ->SetMenuOpen(false);
  }
  folder_menu_anchor_.SetView(nullptr);
  const size_t generation = ++context_menu_generation_;
  context_menu_ = std::make_unique<SidebarBookmarkContextMenu>(
      browser_, bookmark_service_,
      base::BindRepeating(&SidebarBookmarkShelfView::ContextMenuClosed,
                          weak_ptr_factory_.GetWeakPtr(), generation));
  context_menu_->RunAt(source, selection, point, source_type);
}

void SidebarBookmarkShelfView::ContextMenuClosed(size_t generation) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarBookmarkShelfView::ResetContextMenu,
                                weak_ptr_factory_.GetWeakPtr(), generation));
}

void SidebarBookmarkShelfView::ResetContextMenu(size_t generation) {
  if (generation != context_menu_generation_) {
    return;
  }
  context_menu_.reset();
  if (rebuild_after_context_closes_) {
    rebuild_after_context_closes_ = false;
    ScheduleRebuild();
  }
}

std::unique_ptr<views::View> CreateSidebarBookmarkShelfView(Browser* browser) {
  return std::make_unique<SidebarBookmarkShelfView>(browser);
}

bool CanStartBrowserWorkspaceGesture(views::View* sidebar_host,
                                     const gfx::Point& screen_point) {
  if (!sidebar_host || !sidebar_host->IsDrawn() ||
      !sidebar_host->GetBoundsInScreen().Contains(screen_point)) {
    return false;
  }
  for (views::View* child : sidebar_host->children()) {
    if (views::IsViewClass<SidebarBookmarkShelfView>(child) &&
        child->IsDrawn() && child->GetBoundsInScreen().Contains(screen_point)) {
      return false;
    }
  }
  return true;
}

BEGIN_METADATA(SidebarBookmarkShelfView)
END_METADATA

}  // namespace ahoi::sidebar
