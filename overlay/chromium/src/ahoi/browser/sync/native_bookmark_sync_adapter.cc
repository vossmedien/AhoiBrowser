// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"

#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder_children.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"

namespace ahoi::sync {

NativeBookmarkSyncAdapter::NativeBookmarkSyncAdapter(
    BookmarkMergedSurfaceService* service,
    SnapshotCallback callback)
    : service_(service),
      model_(service->bookmark_model()),
      callback_(std::move(callback)) {
  CHECK(service_);
  CHECK(model_);
  model_->AddObserver(this);
  service_->AddObserver(this);
  RequestCapture();
}

NativeBookmarkSyncAdapter::~NativeBookmarkSyncAdapter() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  StopObserving();
}

void NativeBookmarkSyncAdapter::StopObserving() {
  if (service_) {
    service_->RemoveObserver(this);
  }
  if (model_) {
    model_->RemoveObserver(this);
  }
  service_ = nullptr;
  model_ = nullptr;
}

bool NativeBookmarkSyncAdapter::ready() const {
  return service_ && model_ && service_->loaded() && model_->loaded();
}

std::string NativeBookmarkSyncAdapter::Key(
    const bookmarks::BookmarkNode* node) const {
  const auto* root = node;
  while (root && root->parent() && root->parent() != model_->root_node()) {
    root = root->parent();
  }
  const bool account = root == model_->account_bookmark_bar_node() ||
                       root == model_->account_other_node() ||
                       root == model_->account_mobile_node();
  return NativeBookmarkKey(node->uuid(), account);
}

const bookmarks::BookmarkNode* NativeBookmarkSyncAdapter::Find(
    const std::string& key) const {
  base::Uuid uuid;
  bool account = false;
  if (!ParseNativeBookmarkKey(key, &uuid, &account)) {
    return nullptr;
  }
  const auto* node = model_->GetNodeByUuid(
      uuid, account
                ? bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
                : bookmarks::BookmarkModel::NodeTypeForUuidLookup::
                      kLocalOrSyncableNodes);
  return node && !node->is_permanent_node() && !service_->IsNodeManaged(node)
             ? node
             : nullptr;
}

NativeBookmarkSnapshot NativeBookmarkSyncAdapter::Capture() {
  NativeBookmarkSnapshot snapshot;
  snapshot.observation_session = observation_session_;
  if (!ready()) {
    return snapshot;
  }
  local_data_blocked_ = false;
  struct Pending {
    raw_ptr<const bookmarks::BookmarkNode> node;
    std::optional<BookmarkRoot> root;
    size_t index;
  };
  std::vector<Pending> pending;
  const std::pair<BookmarkRoot, BookmarkParentFolder> roots[] = {
      {BookmarkRoot::kBookmarkBar, BookmarkParentFolder::BookmarkBarFolder()},
      {BookmarkRoot::kOther, BookmarkParentFolder::OtherFolder()},
      {BookmarkRoot::kMobile, BookmarkParentFolder::MobileFolder()}};
  for (const auto& [root, folder] : roots) {
    size_t index = 0;
    for (const auto* node : service_->GetChildren(folder)) {
      pending.push_back({node, root, index++});
    }
  }
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto [node, root, index] = pending[i];
    if (service_->IsNodeManaged(node)) {
      continue;
    }
    const std::string key = Key(node);
    NativeBookmarkEntry entry{
        .native_key = key,
        .root = root,
        .kind = node->is_folder() ? BookmarkKind::kFolder : BookmarkKind::kUrl,
        .index = index,
        .title = base::UTF16ToUTF8(node->GetTitle()),
        .url = node->is_url() ? node->url().spec() : std::string(),
        .created_at = node->date_added()};
    if (!ValidateBookmarkContent(entry.kind, entry.title, entry.url,
                                 entry.created_at)) {
      // Keep the native data unchanged and outside the backend snapshot. The
      // whole Bookmark reconciliation waits for a valid observation; otherwise
      // a previously synced value could overwrite the user's local-only edit.
      snapshot.local_data_blocked = true;
      local_data_blocked_ = true;
      continue;
    }
    node->GetMetaInfo(kBookmarkApplyReceiptMetaKey, &entry.apply_receipt);
    entry.explicitly_added = explicitly_added_ids_.contains(node->id());
    const auto previous = runtime_keys_.find(node->id());
    if (previous != runtime_keys_.end() && previous->second != key) {
      entry.previous_native_key = previous->second;
    }
    if (!root) {
      entry.parent_key = Key(node->parent());
    }
    snapshot.entries.push_back(std::move(entry));
    if (node->is_folder()) {
      size_t child_index = 0;
      for (const auto& child : node->children()) {
        pending.push_back({child.get(), std::nullopt, child_index++});
      }
    }
  }
  snapshot.removed_keys = removed_keys_;
  return snapshot;
}

void NativeBookmarkSyncAdapter::RequestCapture() {
  if (applying_ || !service_ || !model_) {
    return;
  }
  ++generation_;
  if (capture_posted_) {
    return;
  }
  capture_posted_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NativeBookmarkSyncAdapter::FlushCapture,
                                weak_ptr_factory_.GetWeakPtr()));
}

void NativeBookmarkSyncAdapter::FlushCapture() {
  capture_posted_ = false;
  if (!ready() || model_->IsDoingExtensiveChanges()) {
    return;
  }
  callback_.Run(generation_, Capture());
}

void NativeBookmarkSyncAdapter::RememberSubtree(
    const bookmarks::BookmarkNode* node) {
  std::vector<const bookmarks::BookmarkNode*> pending{node};
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto* current = pending[i];
    runtime_keys_.try_emplace(current->id(), Key(current));
    for (const auto& child : current->children()) {
      pending.push_back(child.get());
    }
  }
}

void NativeBookmarkSyncAdapter::CaptureRemovedSubtree(
    const bookmarks::BookmarkNode* node) {
  if (applying_ || !ready() || service_->IsNodeManaged(node)) {
    return;
  }
  std::vector<const bookmarks::BookmarkNode*> pending{node};
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto* current = pending[i];
    if (!current->is_permanent_node()) {
      const std::string current_key = Key(current);
      removed_keys_.push_back(current_key);
      const auto old = runtime_keys_.find(current->id());
      if (old != runtime_keys_.end() && old->second != current_key) {
        removed_keys_.push_back(old->second);
      }
    }
    for (const auto& child : current->children()) {
      pending.push_back(child.get());
    }
  }
}

void NativeBookmarkSyncAdapter::BookmarkModelLoaded(bool) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkMergedSurfaceServiceLoaded() {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkModelBeingDeleted() {
  StopObserving();
}
void NativeBookmarkSyncAdapter::BookmarkMergedSurfaceServiceBeingDeleted() {
  StopObserving();
}
void NativeBookmarkSyncAdapter::OnWillMoveBookmarkNode(
    const bookmarks::BookmarkNode* parent,
    size_t index,
    const bookmarks::BookmarkNode*,
    size_t) {
  if (ready()) {
    RememberSubtree(parent->children()[index].get());
  }
}
void NativeBookmarkSyncAdapter::BookmarkNodeMoved(
    const bookmarks::BookmarkNode*,
    size_t,
    const bookmarks::BookmarkNode*,
    size_t) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodeAdded(
    const bookmarks::BookmarkNode* parent,
    size_t index,
    bool) {
  if (!applying_ && ready()) {
    const auto* node = parent->children()[index].get();
    if (!node->is_permanent_node() && !service_->IsNodeManaged(node)) {
      explicitly_added_ids_.insert(node->id());
    }
  }
  RequestCapture();
}
void NativeBookmarkSyncAdapter::OnWillRemoveBookmarks(
    const bookmarks::BookmarkNode* parent,
    size_t,
    const bookmarks::BookmarkNode* node,
    const base::Location&) {
  // Permanent Account-root removal is an account boundary, not a user delete.
  if (parent != model_->root_node()) {
    CaptureRemovedSubtree(node);
  }
}
void NativeBookmarkSyncAdapter::BookmarkNodeRemoved(
    const bookmarks::BookmarkNode*,
    size_t,
    const bookmarks::BookmarkNode*,
    const std::set<GURL>&,
    const base::Location&) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodeChanged(
    const bookmarks::BookmarkNode*) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodeFaviconChanged(
    const bookmarks::BookmarkNode*) {}  // Favicons are local presentation only.
void NativeBookmarkSyncAdapter::BookmarkNodeChildrenReordered(
    const bookmarks::BookmarkNode*) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::OnWillRemoveAllUserBookmarks(
    const base::Location&) {
  if (ready()) {
    // Capture keys even for a previously synced entry whose current content
    // is blocked. An explicit native Clear still owns that deletion.
    for (const auto* root :
         {model_->bookmark_bar_node(), model_->other_node(),
          model_->mobile_node(), model_->account_bookmark_bar_node(),
          model_->account_other_node(), model_->account_mobile_node()}) {
      if (root) {
        CaptureRemovedSubtree(root);
      }
    }
  }
}
void NativeBookmarkSyncAdapter::BookmarkAllUserNodesRemoved(
    const std::set<GURL>&,
    const base::Location&) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodeAdded(const BookmarkParentFolder&,
                                                  size_t) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodesRemoved(
    const BookmarkParentFolder&,
    const base::flat_set<const bookmarks::BookmarkNode*>&) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkNodeMoved(const BookmarkParentFolder&,
                                                  size_t,
                                                  const BookmarkParentFolder&,
                                                  size_t) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkParentFolderChildrenReordered(
    const BookmarkParentFolder&) {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::BookmarkAllUserNodesRemoved() {
  RequestCapture();
}
void NativeBookmarkSyncAdapter::ExtensiveBookmarkChangesEnded() {
  RequestCapture();
}

}  // namespace ahoi::sync
