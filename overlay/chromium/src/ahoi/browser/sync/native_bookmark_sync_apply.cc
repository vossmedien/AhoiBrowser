// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_parent_folder.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "url/gurl.h"

namespace ahoi::sync {

bool NativeBookmarkSyncAdapter::ApplyProjection(
    const BookmarkSyncProjection& projection,
    uint64_t expected_generation) {
  if (!ready() || applying_ || capture_posted_ || local_data_blocked_ ||
      expected_generation != generation_ ||
      !ValidateBookmarkGraph(projection.records)) {
    return false;
  }

  std::map<base::Uuid, const bookmarks::BookmarkNode*> nodes;
  std::map<base::Uuid, std::string> preferred_keys;
  std::map<base::Uuid, std::string> receipts;
  std::map<base::Uuid, const BookmarkRecord*> records;
  for (const auto& record : projection.records) {
    records.emplace(record.id, &record);
  }
  for (const auto& binding : projection.bindings) {
    if (!records.contains(binding.logical_id)) {
      continue;
    }
    preferred_keys.try_emplace(binding.logical_id, binding.native_key);
    if (!binding.apply_receipt.empty()) {
      receipts[binding.logical_id] = binding.apply_receipt;
    }
    if (const auto* node = Find(binding.native_key)) {
      const auto [found, added] = nodes.emplace(binding.logical_id, node);
      if (!added && found->second != node) {
        return false;
      }
      if ((records.at(binding.logical_id)->kind == BookmarkKind::kFolder) !=
          node->is_folder()) {
        return false;
      }
      preferred_keys[binding.logical_id] = binding.native_key;
    }
  }

  // A provider page may contain detached descendants. Only a complete live
  // ancestry to a native permanent root is safe to materialize. Use an
  // iterative adjacency walk, not depth-recursive native mutations.
  std::map<base::Uuid, std::vector<const BookmarkRecord*>> children;
  std::vector<const BookmarkRecord*> order;
  for (const auto& record : projection.records) {
    if (record.tombstone) {
      continue;
    }
    if (record.parent_id) {
      children[*record.parent_id].push_back(&record);
    } else {
      order.push_back(&record);
    }
  }
  const auto before = [](const BookmarkRecord* a, const BookmarkRecord* b) {
    return a->sort_key == b->sort_key ? a->id < b->id
                                      : a->sort_key < b->sort_key;
  };
  std::ranges::sort(order, before);
  for (auto& [id, group] : children) {
    std::ranges::sort(group, before);
  }
  for (size_t i = 0; i < order.size(); ++i) {
    const auto child = children.find(order[i]->id);
    if (child != children.end()) {
      order.insert(order.end(), child->second.begin(), child->second.end());
    }
  }
  const auto permanent = [&](BookmarkRoot root,
                             bool account) -> const bookmarks::BookmarkNode* {
    switch (root) {
      case BookmarkRoot::kBookmarkBar:
        return account ? model_->account_bookmark_bar_node()
                       : model_->bookmark_bar_node();
      case BookmarkRoot::kOther:
        return account ? model_->account_other_node() : model_->other_node();
      case BookmarkRoot::kMobile:
        return account ? model_->account_mobile_node() : model_->mobile_node();
    }
  };
  base::AutoReset<bool> applying(&applying_, true);
  model_->BeginExtensiveChanges();
  base::ScopedClosureRunner finish_batch(base::BindOnce(
      &bookmarks::BookmarkModel::EndExtensiveChanges, model_->AsWeakPtr()));
  for (const auto* record : order) {
    const auto key = preferred_keys.find(record->id);
    if (key == preferred_keys.end()) {
      return false;
    }
    base::Uuid uuid;
    bool account = false;
    if (!ParseNativeBookmarkKey(key->second, &uuid, &account)) {
      return false;
    }
    const bookmarks::BookmarkNode* parent = nullptr;
    if (record->parent_id) {
      const auto found = nodes.find(*record->parent_id);
      if (found == nodes.end()) {
        continue;  // Account-detached ancestor.
      }
      parent = found->second;
    } else {
      parent = permanent(*record->root_kind, account);
      if (!parent) {
        continue;  // Account removal is not permission to restore it.
      }
    }
    auto found = nodes.find(record->id);
    const bookmarks::BookmarkNode* node =
        found == nodes.end() ? nullptr : found->second;
    if (!node) {
      const auto storage =
          Key(parent).starts_with("account:")
              ? bookmarks::BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
              : bookmarks::BookmarkModel::NodeTypeForUuidLookup::
                    kLocalOrSyncableNodes;
      // Never hand a colliding GUID to native Add* (it has a CHECK invariant).
      if (model_->GetNodeByUuid(uuid, storage)) {
        return false;
      }
      node = record->kind == BookmarkKind::kFolder
                 ? model_->AddFolder(parent, parent->children().size(),
                                     base::UTF8ToUTF16(record->title), nullptr,
                                     record->created_at, uuid)
                 : model_->AddURL(parent, parent->children().size(),
                                  base::UTF8ToUTF16(record->title),
                                  GURL(record->url), nullptr,
                                  record->created_at, uuid);
      if (!node) {
        return false;
      }
      runtime_keys_[node->id()] = key->second;
      nodes.emplace(record->id, node);
    } else {
      if (node->parent() != parent) {
        model_->Move(node, parent, parent->children().size());
      }
      if (node->GetTitle() != base::UTF8ToUTF16(record->title)) {
        model_->SetTitle(node, base::UTF8ToUTF16(record->title),
                         bookmarks::metrics::BookmarkEditSource::kOther);
      }
      if (node->is_url() && node->url() != GURL(record->url)) {
        model_->SetURL(node, GURL(record->url),
                       bookmarks::metrics::BookmarkEditSource::kOther);
      }
      if (node->date_added() != record->created_at) {
        model_->SetDateAdded(node, record->created_at);
      }
    }
    if (const auto receipt = receipts.find(record->id);
        receipt != receipts.end()) {
      model_->SetNodeMetaInfo(node, kBookmarkApplyReceiptMetaKey,
                              receipt->second);
    }
  }

  // Reorder through the merged service: a permanent-root move can affect only
  // the cross-storage visual order. No browser or foreground tab is created.
  std::map<std::string, size_t> next_index;
  for (const auto* record : order) {
    const auto found = nodes.find(record->id);
    if (found == nodes.end()) {
      continue;
    }
    const auto* node = found->second;
    const std::string group =
        record->parent_id
            ? record->parent_id->AsLowercaseString()
            : "root:" + std::to_string(static_cast<int>(*record->root_kind));
    size_t& index = next_index[group];
    const BookmarkParentFolder folder =
        BookmarkParentFolder::FromFolderNode(node->parent());
    const size_t current = service_->GetIndexOf(node);
    if (current != index) {
      service_->Move(node, folder, current < index ? index + 1 : index,
                     nullptr);
    }
    ++index;
  }

  // Remove descendants first so no map entry can dangle after a parent goes.
  // Resolve each native key again, since a Move can change its storage/GUID.
  std::vector<std::pair<size_t, std::string>> deletions;
  std::map<const bookmarks::BookmarkNode*, bool> deletable;
  for (const auto& [id, node] : nodes) {
    deletable[node] = records.at(id)->tombstone;
  }
  std::vector<std::pair<const bookmarks::BookmarkNode*, size_t>> walk{
      {model_->root_node(), 0}};
  std::map<const bookmarks::BookmarkNode*, size_t> depths;
  for (size_t i = 0; i < walk.size(); ++i) {
    const auto [node, depth] = walk[i];
    depths[node] = depth;
    for (const auto& child : node->children()) {
      walk.emplace_back(child.get(), depth + 1);
    }
  }
  for (auto it = walk.rbegin(); it != walk.rend(); ++it) {
    for (const auto& child : it->first->children()) {
      deletable[it->first] = deletable[it->first] && deletable[child.get()];
    }
  }
  for (const auto& record : projection.records) {
    if (!record.tombstone) {
      continue;
    }
    const auto found = nodes.find(record.id);
    if (found == nodes.end()) {
      continue;
    }
    // A parent tombstone may precede a child's provider page, and local-only
    // descendants may never belong to this projection. Do not let a native
    // subtree removal erase either. Retry on the next complete domain update.
    if (!deletable[found->second]) {
      continue;
    }
    deletions.emplace_back(depths.at(found->second), Key(found->second));
  }
  std::ranges::sort(deletions, std::greater<>());
  for (const auto& [depth, key] : deletions) {
    if (const auto* node = Find(key)) {
      model_->Remove(node, bookmarks::metrics::BookmarkEditSource::kOther,
                     FROM_HERE);
    }
  }
  return true;
}

void NativeBookmarkSyncAdapter::AcknowledgeCapture(uint64_t generation) {
  if (generation != generation_ || !ready()) {
    return;
  }
  removed_keys_.clear();
  explicitly_added_ids_.clear();
  runtime_keys_.clear();
  std::vector<const bookmarks::BookmarkNode*> pending{model_->root_node()};
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto* node = pending[i];
    if (!node->is_permanent_node() && !service_->IsNodeManaged(node)) {
      runtime_keys_.emplace(node->id(), Key(node));
    }
    for (const auto& child : node->children()) {
      pending.push_back(child.get());
    }
  }
}

}  // namespace ahoi::sync
