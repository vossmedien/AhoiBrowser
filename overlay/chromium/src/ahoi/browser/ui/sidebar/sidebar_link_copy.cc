// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_link_copy.h"

#include <unordered_set>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"

namespace ahoi::sidebar {

tab_tree::TabTreeStore::Result BuildOrderedLinkList(
    tab_tree::TabTreeStore* store,
    const base::Uuid& workspace_id,
    std::optional<base::Uuid> folder_id,
    std::u16string* links) {
  if (!store || !workspace_id.is_valid() || !links ||
      (folder_id.has_value() && !folder_id->is_valid())) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }

  if (folder_id.has_value()) {
    tab_tree::TreeNode folder;
    const tab_tree::TabTreeStore::Result folder_result =
        store->GetNode(*folder_id, &folder);
    if (folder_result != tab_tree::TabTreeStore::Result::kOk) {
      return folder_result;
    }
    if (folder.tombstone || folder.workspace_id != workspace_id ||
        folder.type != tab_tree::TreeNodeType::kFolder) {
      return tab_tree::TabTreeStore::Result::kInvalidArgument;
    }
  }

  std::vector<tab_tree::TreeNode> children;
  const tab_tree::TabTreeStore::Result root_result =
      store->GetChildren(workspace_id, folder_id, &children);
  if (root_result != tab_tree::TabTreeStore::Result::kOk) {
    return root_result;
  }

  std::vector<tab_tree::TreeNode> pending;
  pending.reserve(children.size());
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    pending.push_back(std::move(*it));
  }

  std::unordered_set<base::Uuid, base::UuidHash> visited;
  std::u16string ordered_links;
  while (!pending.empty()) {
    tab_tree::TreeNode node = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(node.id).second) {
      return tab_tree::TabTreeStore::Result::kDatabaseError;
    }
    if (node.type == tab_tree::TreeNodeType::kSavedPage) {
      if (!node.url.is_valid() || node.url.is_empty()) {
        return tab_tree::TabTreeStore::Result::kDatabaseError;
      }
      if (!ordered_links.empty()) {
        ordered_links.push_back(u'\n');
      }
      ordered_links.append(base::UTF8ToUTF16(node.url.spec()));
      continue;
    }

    children.clear();
    const tab_tree::TabTreeStore::Result child_result =
        store->GetChildren(workspace_id, node.id, &children);
    if (child_result != tab_tree::TabTreeStore::Result::kOk) {
      return child_result;
    }
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      pending.push_back(std::move(*it));
    }
  }

  *links = std::move(ordered_links);
  return tab_tree::TabTreeStore::Result::kOk;
}

}  // namespace ahoi::sidebar
