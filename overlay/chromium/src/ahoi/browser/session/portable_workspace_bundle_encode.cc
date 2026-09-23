// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

#include "ahoi/browser/session/portable_workspace_bundle.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "ahoi/browser/tab_tree/shared_tab_target_policy.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace ahoi::session {
namespace {

constexpr size_t kMaximumWorkspaceCount = 128;
constexpr size_t kMaximumNodeCount = 10'000;
constexpr size_t kMaximumSplitCount = 2'500;
constexpr size_t kMaximumArchiveCount = 10'000;
constexpr size_t kMaximumTextBytes = 65'536;
constexpr size_t kMaximumIconBytes = 256;
constexpr size_t kMaximumSortKeyBytes = 1'024;
constexpr size_t kInputStringBudget = 8 * 1024 * 1024;

class StringBudget {
 public:
  bool Charge(std::string_view value, size_t per_field_limit) {
    if (value.size() > per_field_limit ||
        value.size() > kInputStringBudget - consumed_) {
      return false;
    }
    consumed_ += value.size();
    return true;
  }

  bool Charge(std::u16string_view value, size_t per_field_limit) {
    if (value.size() > per_field_limit ||
        value.size() > (kInputStringBudget - consumed_) / 3) {
      return false;
    }
    consumed_ += value.size() * 3;
    return true;
  }

 private:
  size_t consumed_ = 0;
};

bool ValidSortKey(std::string_view value, StringBudget* budget) {
  return !value.empty() && budget->Charge(value, kMaximumSortKeyBytes) &&
         std::ranges::all_of(
             value, [](unsigned char c) { return c >= 0x21 && c <= 0x7e; });
}

bool ValidArchivePolicy(sync::SharedArchivePolicy policy) {
  return policy >= sync::SharedArchivePolicy::kNever &&
         policy <= sync::SharedArchivePolicy::kThirtyDays;
}

std::optional<base::DictValue> TargetValue(const sync::SharedTabTarget& target,
                                           bool is_temporary,
                                           StringBudget* budget) {
  if (!tab_tree::IsValidSharedPageTarget(target, is_temporary) ||
      target.kind == sync::SharedTabTargetKind::kLocalOnly ||
      !budget->Charge(target.url, 131'072)) {
    return std::nullopt;
  }
  base::DictValue value;
  value.Set("kind",
            target.kind == sync::SharedTabTargetKind::kWeb ? "web" : "new-tab");
  if (target.kind == sync::SharedTabTargetKind::kWeb) {
    value.Set("url", target.url);
  }
  return value;
}

std::optional<base::DictValue> SplitValue(
    const sync::SharedSplitMetadata& split,
    const std::map<base::Uuid, base::Uuid>& page_workspaces) {
  if (!sync::ValidateSplitMetadata(split) ||
      !std::ranges::all_of(split.topology.member_ids,
                           [&](const base::Uuid& id) {
                             const auto page = page_workspaces.find(id);
                             return page != page_workspaces.end() &&
                                    page->second == split.workspace_id;
                           })) {
    return std::nullopt;
  }
  base::DictValue value;
  value.Set("id", split.id.AsLowercaseString());
  value.Set("workspace_id", split.workspace_id.AsLowercaseString());
  value.Set("axis", static_cast<int>(split.topology.axis));
  value.Set("arrangement", static_cast<int>(split.topology.arrangement));
  value.Set("primary_ratio", static_cast<int>(split.ratios.primary));
  value.Set("secondary_ratio", static_cast<int>(split.ratios.secondary));
  base::ListValue members;
  for (const base::Uuid& id : split.topology.member_ids) {
    members.Append(id.AsLowercaseString());
  }
  value.Set("member_ids", std::move(members));
  return value;
}

bool ValidParent(const std::optional<base::Uuid>& parent_id,
                 const base::Uuid& workspace_id,
                 const std::map<base::Uuid, base::Uuid>& folder_workspaces) {
  if (!parent_id) {
    return true;
  }
  const auto folder = folder_workspaces.find(*parent_id);
  return folder != folder_workspaces.end() && folder->second == workspace_id;
}

bool ValidHierarchy(const std::vector<tab_tree::PortableWorkspaceNode>& nodes,
                    const std::map<base::Uuid, base::Uuid>& folder_workspaces) {
  std::map<base::Uuid, std::optional<base::Uuid>> folder_parents;
  for (const auto& node : nodes) {
    if (!ValidParent(node.parent_id, node.workspace_id, folder_workspaces)) {
      return false;
    }
    if (node.type == tab_tree::TreeNodeType::kFolder) {
      folder_parents.emplace(node.id, node.parent_id);
    }
  }
  for (const auto& node : nodes) {
    std::optional<base::Uuid> parent = node.parent_id;
    size_t depth = 0;
    while (parent) {
      if (++depth > 64 || *parent == node.id) {
        return false;
      }
      const auto folder = folder_parents.find(*parent);
      if (folder == folder_parents.end()) {
        return false;
      }
      parent = folder->second;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> EncodePortableWorkspaceBundle(
    const PortableWorkspaceStructure& structure) {
  if (structure.tree.workspaces.empty() ||
      structure.tree.workspaces.size() > kMaximumWorkspaceCount ||
      structure.tree.nodes.size() > kMaximumNodeCount ||
      structure.splits.size() > kMaximumSplitCount ||
      structure.archives.size() > kMaximumArchiveCount) {
    return std::nullopt;
  }

  StringBudget budget;
  std::set<base::Uuid> workspace_ids;
  std::set<base::Uuid> node_ids;
  std::map<base::Uuid, base::Uuid> folder_workspaces;
  std::map<base::Uuid, base::Uuid> page_workspaces;
  base::ListValue workspaces;
  for (const auto& workspace : structure.tree.workspaces) {
    if (!workspace.id.is_valid() ||
        !workspace_ids.insert(workspace.id).second ||
        !budget.Charge(workspace.name, kMaximumTextBytes) ||
        !budget.Charge(workspace.icon, kMaximumIconBytes) ||
        !ValidSortKey(workspace.sort_key, &budget) ||
        !ValidArchivePolicy(workspace.archive_policy)) {
      return std::nullopt;
    }
    base::DictValue value;
    value.Set("id", workspace.id.AsLowercaseString());
    value.Set("name", base::UTF16ToUTF8(workspace.name));
    value.Set("icon", base::UTF16ToUTF8(workspace.icon));
    value.Set("sort_key", workspace.sort_key);
    value.Set("archive_policy", static_cast<int>(workspace.archive_policy));
    if (workspace.accent_argb) {
      value.Set("accent_argb",
                base::StringPrintf("%08x", *workspace.accent_argb));
    }
    workspaces.Append(std::move(value));
  }

  for (const auto& node : structure.tree.nodes) {
    if (!node.id.is_valid() || !node_ids.insert(node.id).second ||
        !workspace_ids.contains(node.workspace_id) ||
        !budget.Charge(node.title, kMaximumTextBytes) ||
        !budget.Charge(node.icon, kMaximumIconBytes) ||
        !ValidSortKey(node.sort_key, &budget)) {
      return std::nullopt;
    }
    auto& index = node.type == tab_tree::TreeNodeType::kFolder
                      ? folder_workspaces
                      : page_workspaces;
    if (!index.emplace(node.id, node.workspace_id).second) {
      return std::nullopt;
    }
  }
  if (!ValidHierarchy(structure.tree.nodes, folder_workspaces)) {
    return std::nullopt;
  }

  base::ListValue nodes;
  for (const auto& node : structure.tree.nodes) {
    if (!ValidParent(node.parent_id, node.workspace_id, folder_workspaces) ||
        (node.parent_id && *node.parent_id == node.id)) {
      return std::nullopt;
    }
    base::DictValue value;
    value.Set("id", node.id.AsLowercaseString());
    value.Set("workspace_id", node.workspace_id.AsLowercaseString());
    if (node.parent_id) {
      value.Set("parent_id", node.parent_id->AsLowercaseString());
    }
    value.Set("kind",
              node.type == tab_tree::TreeNodeType::kFolder ? "folder" : "page");
    value.Set("title", base::UTF16ToUTF8(node.title));
    value.Set("icon", base::UTF16ToUTF8(node.icon));
    value.Set("sort_key", node.sort_key);
    if (node.accent_argb) {
      value.Set("accent_argb", base::StringPrintf("%08x", *node.accent_argb));
    }
    if (node.type == tab_tree::TreeNodeType::kFolder) {
      if (node.is_temporary || node.target || node.home_target) {
        return std::nullopt;
      }
    } else if (node.type == tab_tree::TreeNodeType::kSavedPage && node.target) {
      auto target = TargetValue(*node.target, node.is_temporary, &budget);
      if (!target) {
        return std::nullopt;
      }
      value.Set("is_temporary", node.is_temporary);
      value.Set("target", std::move(*target));
      if (node.home_target) {
        auto home = TargetValue(*node.home_target, false, &budget);
        if (!home ||
            node.home_target->kind != sync::SharedTabTargetKind::kWeb) {
          return std::nullopt;
        }
        value.Set("home_target", std::move(*home));
      }
    } else {
      return std::nullopt;
    }
    nodes.Append(std::move(value));
  }

  base::ListValue splits;
  std::set<base::Uuid> split_ids;
  for (const auto& split : structure.splits) {
    if (!split_ids.insert(split.id).second ||
        !workspace_ids.contains(split.workspace_id)) {
      return std::nullopt;
    }
    auto encoded = SplitValue(split, page_workspaces);
    if (!encoded) {
      return std::nullopt;
    }
    splits.Append(std::move(*encoded));
  }

  base::ListValue archives;
  std::set<base::Uuid> archive_ids;
  for (const auto& archive : structure.archives) {
    if (!archive.id.is_valid() || !archive_ids.insert(archive.id).second ||
        !workspace_ids.contains(archive.snapshot.workspace_id) ||
        !sync::ValidateArchiveSnapshot(archive.snapshot) ||
        archive.id != sync::ArchiveIdForSnapshot(archive.snapshot) ||
        archive.archived_at <= base::Time::UnixEpoch() ||
        (archive.reason != sync::SharedArchiveReason::kAutomatic &&
         archive.reason != sync::SharedArchiveReason::kManual)) {
      return std::nullopt;
    }
    base::DictValue value;
    value.Set("id", archive.id.AsLowercaseString());
    value.Set("workspace_id",
              archive.snapshot.workspace_id.AsLowercaseString());
    value.Set("reason", static_cast<int>(archive.reason));
    value.Set(
        "archived_at_us",
        base::NumberToString(
            archive.archived_at.ToDeltaSinceWindowsEpoch().InMicroseconds()));
    base::ListValue pages;
    std::map<base::Uuid, base::Uuid> archived_page_workspaces;
    for (const auto& page : archive.snapshot.pages) {
      if (node_ids.contains(page.tree_node_id)) {
        return std::nullopt;
      }
      archived_page_workspaces.emplace(page.tree_node_id,
                                       archive.snapshot.workspace_id);
      if (!ValidParent(page.parent_id, archive.snapshot.workspace_id,
                       folder_workspaces) ||
          !budget.Charge(page.title, kMaximumTextBytes) ||
          !ValidSortKey(page.sort_key, &budget)) {
        return std::nullopt;
      }
      auto target = TargetValue(page.target, true, &budget);
      if (!target) {
        return std::nullopt;
      }
      base::DictValue item;
      item.Set("id", page.tree_node_id.AsLowercaseString());
      if (page.parent_id) {
        item.Set("parent_id", page.parent_id->AsLowercaseString());
      }
      item.Set("sort_key", page.sort_key);
      item.Set("title", page.title);
      item.Set("target", std::move(*target));
      if (page.home_target) {
        auto home = TargetValue(*page.home_target, false, &budget);
        if (!home ||
            page.home_target->kind != sync::SharedTabTargetKind::kWeb) {
          return std::nullopt;
        }
        item.Set("home_target", std::move(*home));
      }
      pages.Append(std::move(item));
    }
    value.Set("pages", std::move(pages));
    if (archive.snapshot.split) {
      if (!split_ids.insert(archive.snapshot.split->id).second) {
        return std::nullopt;
      }
      auto split =
          SplitValue(*archive.snapshot.split, archived_page_workspaces);
      // Archived members are absent from the live tree. Their topology is
      // already validated with the archive's exact ordered page IDs above.
      if (!split) {
        return std::nullopt;
      }
      value.Set("split", std::move(*split));
    }
    archives.Append(std::move(value));
  }

  base::DictValue root;
  root.Set("format", "ahoi-workspaces");
  root.Set("version", kPortableWorkspaceBundleVersion);
  root.Set("workspaces", std::move(workspaces));
  root.Set("nodes", std::move(nodes));
  root.Set("splits", std::move(splits));
  root.Set("archives", std::move(archives));
  std::optional<std::string> encoded = base::WriteJson(root);
  return encoded && encoded->size() <= kMaximumPortableWorkspaceBundleBytes
             ? encoded
             : std::nullopt;
}

}  // namespace ahoi::session
