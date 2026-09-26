// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "base/memory/raw_ref.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"

namespace ahoi::importer::arc::internal {

namespace {

class ArcPlanBuilder {
 public:
  ArcPlanBuilder(const std::map<std::string, SourceSpace>& spaces,
                 const std::map<std::string, SourceItem>& items,
                 const std::vector<std::string>& ordered_space_ids)
      : spaces_(spaces), items_(items), ordered_space_ids_(ordered_space_ids) {}

  ArcPlanBuilder(const ArcPlanBuilder&) = delete;
  ArcPlanBuilder& operator=(const ArcPlanBuilder&) = delete;

  ArcImportStatus Run(ArcImportPlan* plan) {
    // The parser already recorded the source counts in `plan`'s stats.
    plan_ = std::move(*plan);
    if (!BuildPlan()) {
      return status_;
    }
    *plan = std::move(plan_);
    return ArcImportStatus::kOk;
  }

 private:
  bool Fail(ArcImportStatus status) {
    status_ = status;
    return false;
  }

  std::optional<ArcSplitDescriptor> BuildSplitDescriptor(
      const SourceItem& item,
      const base::Uuid& folder_id) const {
    if (!item.split_metadata_valid || item.children.size() < 2 ||
        item.children.size() > kMaxSplitMembers ||
        !item.split_focus_item_id.has_value() ||
        std::find(item.children.begin(), item.children.end(),
                  *item.split_focus_item_id) == item.children.end()) {
      return std::nullopt;
    }

    std::set<std::string> child_ids(item.children.begin(), item.children.end());
    for (const auto& [factor_id, factor] : item.split_width_factors) {
      if (!child_ids.contains(factor_id) || !std::isfinite(factor) ||
          factor <= 0.0) {
        return std::nullopt;
      }
    }

    ArcSplitDescriptor descriptor{
        .folder_node_id = folder_id,
        .orientation = item.split_orientation,
        .focused_member_node_id =
            MakeDeterministicArcId(kItemIdDomain, *item.split_focus_item_id),
    };
    std::vector<double> weights(item.children.size(), 0.0);
    double known_total = 0.0;
    size_t missing_count = 0;
    for (size_t index = 0; index < item.children.size(); ++index) {
      const auto child_it = items_->find(item.children[index]);
      if (child_it == items_->end() ||
          child_it->second.kind != SourceItemKind::kTab ||
          !child_it->second.children.empty() ||
          !IsSafeImportUrl(child_it->second.url)) {
        return std::nullopt;
      }
      descriptor.member_node_ids.push_back(
          MakeDeterministicArcId(kItemIdDomain, child_it->second.id));
      const auto factor_it = item.split_width_factors.find(child_it->second.id);
      if (factor_it == item.split_width_factors.end()) {
        ++missing_count;
      } else {
        weights[index] = factor_it->second;
        known_total += factor_it->second;
      }
    }
    if (missing_count > 0) {
      const double inferred =
          known_total > 0.0 && known_total < 1.0
              ? (1.0 - known_total) / static_cast<double>(missing_count)
              : (known_total > 0.0
                     ? known_total / static_cast<double>(item.children.size() -
                                                         missing_count)
                     : 1.0);
      for (double& weight : weights) {
        if (weight == 0.0) {
          weight = inferred;
        }
      }
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (!std::isfinite(total) || total <= 0.0) {
      return std::nullopt;
    }
    for (double weight : weights) {
      descriptor.normalized_ratios.push_back(weight / total);
    }
    return descriptor;
  }

  bool VisitItem(const std::string& item_id,
                 const std::string& expected_parent_id,
                 const base::Uuid& workspace_id,
                 std::optional<base::Uuid> destination_parent_id,
                 std::string sort_key,
                 size_t depth,
                 bool emit) {
    if (depth > kMaxTreeDepth) {
      return Fail(ArcImportStatus::kLimitExceeded);
    }
    const auto item_it = items_->find(item_id);
    if (item_it == items_->end() || !item_it->second.parent_id.has_value() ||
        *item_it->second.parent_id != expected_parent_id ||
        !claimed_item_ids_.insert(item_id).second) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    const SourceItem& item = item_it->second;
    if (item.kind == SourceItemKind::kContainer) {
      return Fail(ArcImportStatus::kGraphViolation);
    }

    if (!emit || item.kind == SourceItemKind::kUnsupported) {
      ++plan_.stats.skipped_unsupported_item_count;
      size_t ignored_position = 0;
      for (const std::string& child_id : item.children) {
        if (!VisitItem(child_id, item.id, workspace_id, destination_parent_id,
                       SortKey(ignored_position++), depth + 1,
                       /*emit=*/false)) {
          return false;
        }
      }
      return true;
    }

    if (item.kind == SourceItemKind::kTab) {
      if (!item.children.empty()) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      const GURL url(item.url);
      if (!IsSafeImportUrl(item.url)) {
        ++plan_.stats.skipped_unsafe_url_count;
        return true;
      }
      std::string title = item.title.empty() ? item.saved_title : item.title;
      if (title.empty()) {
        title = std::string(url.host());
        if (title.empty()) {
          title = "Imported Page";
        }
      }
      plan_.tree.nodes.push_back(tab_tree::TreeNode{
          .id = MakeDeterministicArcId(kItemIdDomain, item.id),
          .workspace_id = workspace_id,
          .parent_id = destination_parent_id,
          .type = tab_tree::TreeNodeType::kSavedPage,
          .title = base::UTF8ToUTF16(title),
          .url = url,
          .sort_key = std::move(sort_key),
          .created_at = base::Time::UnixEpoch(),
          .modified_at = base::Time::UnixEpoch(),
      });
      ++plan_.stats.imported_page_count;
      return true;
    }

    const base::Uuid folder_id = MakeDeterministicArcId(kItemIdDomain, item.id);
    const std::optional<ArcSplitDescriptor> split_descriptor =
        item.kind == SourceItemKind::kSplit
            ? BuildSplitDescriptor(item, folder_id)
            : std::nullopt;
    std::string title = item.title;
    if (title.empty()) {
      title = item.kind == SourceItemKind::kSplit ? "Split View"
                                                  : "Untitled Folder";
    }
    plan_.tree.nodes.push_back(tab_tree::TreeNode{
        .id = folder_id,
        .workspace_id = workspace_id,
        .parent_id = destination_parent_id,
        .type = tab_tree::TreeNodeType::kFolder,
        .title = base::UTF8ToUTF16(title),
        .icon = split_descriptor.has_value() ? u"split" : u"folder",
        .sort_key = std::move(sort_key),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_folder_count;

    size_t child_position = 0;
    for (const std::string& child_id : item.children) {
      if (!VisitItem(child_id, item.id, workspace_id, folder_id,
                     SortKey(child_position++), depth + 1, /*emit=*/true)) {
        return false;
      }
    }
    if (item.kind == SourceItemKind::kSplit) {
      if (split_descriptor.has_value()) {
        plan_.splits.push_back(*split_descriptor);
        ++plan_.stats.imported_split_count;
      } else {
        plan_.degraded_split_folder_node_ids.push_back(folder_id);
        ++plan_.stats.degraded_split_count;
      }
    }
    return true;
  }

  bool BuildGlobalTopApps(const base::Uuid& workspace_id,
                          size_t* top_level_position) {
    const SourceItem* top_apps = nullptr;
    for (const auto& [id, item] : *items_) {
      if (!item.is_top_apps_container) {
        continue;
      }
      if (top_apps || item.parent_id.has_value() ||
          !claimed_item_ids_.insert(id).second) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      top_apps = &item;
    }
    if (!top_apps || top_apps->children.empty()) {
      return true;
    }

    const base::Uuid folder_id =
        MakeDeterministicArcId(kItemIdDomain, top_apps->id);
    plan_.tree.nodes.push_back(tab_tree::TreeNode{
        .id = folder_id,
        .workspace_id = workspace_id,
        .type = tab_tree::TreeNodeType::kFolder,
        .title = u"Arc Favorites",
        .icon = u"star",
        .sort_key = SortKey((*top_level_position)++),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_folder_count;
    const size_t nodes_before = plan_.tree.nodes.size();
    const size_t pages_before = plan_.stats.imported_page_count;
    size_t child_position = 0;
    for (const std::string& child_id : top_apps->children) {
      if (!VisitItem(child_id, top_apps->id, workspace_id, folder_id,
                     SortKey(child_position++), /*depth=*/1,
                     /*emit=*/true)) {
        return false;
      }
    }
    plan_.stats.imported_global_top_app_count +=
        plan_.stats.imported_page_count - pages_before;
    for (size_t index = nodes_before; index < plan_.tree.nodes.size();
         ++index) {
      if (plan_.tree.nodes[index].type == tab_tree::TreeNodeType::kSavedPage) {
        plan_.global_top_app_page_node_ids.push_back(
            plan_.tree.nodes[index].id);
      }
    }
    return true;
  }

  bool BuildWorkspace(const SourceSpace& space,
                      size_t workspace_position,
                      bool include_global_top_apps) {
    std::string title =
        space.title.empty() ? "Imported Workspace" : space.title;
    const base::Uuid workspace_id =
        MakeDeterministicArcId(kWorkspaceIdDomain, space.id);
    if (!workspace_id.is_valid()) {
      return Fail(ArcImportStatus::kInvalidText);
    }
    plan_.tree.workspaces.push_back(tab_tree::Workspace{
        .id = workspace_id,
        .name = base::UTF8ToUTF16(title),
        .sort_key = SortKey(workspace_position),
        .created_at = base::Time::UnixEpoch(),
        .modified_at = base::Time::UnixEpoch(),
    });
    ++plan_.stats.imported_workspace_count;

    size_t top_level_position = 0;
    if (include_global_top_apps &&
        !BuildGlobalTopApps(workspace_id, &top_level_position)) {
      return false;
    }
    for (const std::string& root_id : space.root_container_ids) {
      const auto root_it = items_->find(root_id);
      if (root_it == items_->end() ||
          root_it->second.kind != SourceItemKind::kContainer ||
          root_it->second.parent_id.has_value() ||
          !root_it->second.container_space_id.has_value() ||
          *root_it->second.container_space_id != space.id ||
          !claimed_item_ids_.insert(root_id).second) {
        return Fail(ArcImportStatus::kGraphViolation);
      }
      for (const std::string& child_id : root_it->second.children) {
        if (!VisitItem(child_id, root_id, workspace_id, std::nullopt,
                       SortKey(top_level_position++), /*depth=*/1,
                       /*emit=*/true)) {
          return false;
        }
      }
    }
    return true;
  }

  bool BuildPlan() {
    for (size_t index = 0; index < ordered_space_ids_->size(); ++index) {
      const auto space_it = spaces_->find((*ordered_space_ids_)[index]);
      if (space_it == spaces_->end() ||
          !BuildWorkspace(space_it->second, index,
                          /*include_global_top_apps=*/index == 0)) {
        return false;
      }
    }
    if (claimed_item_ids_.size() != items_->size()) {
      return Fail(ArcImportStatus::kGraphViolation);
    }
    return true;
  }

  const base::raw_ref<const std::map<std::string, SourceSpace>> spaces_;
  const base::raw_ref<const std::map<std::string, SourceItem>> items_;
  const base::raw_ref<const std::vector<std::string>> ordered_space_ids_;
  // Same default as ArcParser: a failure without Fail() reports it.
  ArcImportStatus status_ = ArcImportStatus::kInvalidJson;
  ArcImportPlan plan_;
  std::set<std::string> claimed_item_ids_;
};

}  // namespace

ArcImportStatus BuildArcImportPlan(
    const std::map<std::string, SourceSpace>& spaces,
    const std::map<std::string, SourceItem>& items,
    const std::vector<std::string>& ordered_space_ids,
    ArcImportPlan* plan) {
  return ArcPlanBuilder(spaces, items, ordered_space_ids).Run(plan);
}

}  // namespace ahoi::importer::arc::internal
