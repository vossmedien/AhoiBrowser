// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/sidebar/sidebar_tree_controller.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "base/check.h"

namespace ahoi::sidebar {

namespace {

void AddUniqueParent(std::vector<std::optional<base::Uuid>>* parents,
                     std::optional<base::Uuid> parent_id) {
  if (std::ranges::find(*parents, parent_id) == parents->end()) {
    parents->push_back(parent_id);
  }
}

tab_tree::TabTreeStore::Result StoreResultForDropValidation(
    SidebarTreeController::DropValidationResult validation) {
  switch (validation) {
    case SidebarTreeController::DropValidationResult::kAllowed:
      return tab_tree::TabTreeStore::Result::kOk;
    case SidebarTreeController::DropValidationResult::kTargetNotFound:
    case SidebarTreeController::DropValidationResult::kSourceNotFound:
      return tab_tree::TabTreeStore::Result::kNotFound;
    case SidebarTreeController::DropValidationResult::kCycle:
      return tab_tree::TabTreeStore::Result::kCycle;
    case SidebarTreeController::DropValidationResult::kStoreError:
      return tab_tree::TabTreeStore::Result::kDatabaseError;
    case SidebarTreeController::DropValidationResult::kInvalidArgument:
    case SidebarTreeController::DropValidationResult::kTargetNotFolder:
    case SidebarTreeController::DropValidationResult::kNoOp:
    case SidebarTreeController::DropValidationResult::kNoOrderingSpace:
      return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
}

}  // namespace

SidebarTreeController::SidebarTreeController(tab_tree::TabTreeStore* store)
    : store_(store) {
  CHECK(store_);
  store_observation_.Observe(store_);
}

SidebarTreeController::~SidebarTreeController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

tab_tree::TabTreeStore::Result SidebarTreeController::ActivateWorkspace(
    const base::Uuid& workspace_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::Workspace workspace;
  const tab_tree::TabTreeStore::Result result =
      store_->GetWorkspace(workspace_id, &workspace);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }
  if (workspace.tombstone || !view_model_.ResetWorkspace(workspace_id)) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  return RefreshChildren(std::nullopt);
}

tab_tree::TabTreeStore::Result SidebarTreeController::ExpandNode(
    const base::Uuid& node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const tab_tree::TreeNode* node = view_model_.GetNode(node_id);
  if (!node || node->type != tab_tree::TreeNodeType::kFolder ||
      !view_model_.GetRowForNode(node_id).has_value()) {
    return tab_tree::TabTreeStore::Result::kNotFound;
  }
  if (!view_model_.AreChildrenLoaded(node_id)) {
    const tab_tree::TabTreeStore::Result result = RefreshChildren(node_id);
    if (result != tab_tree::TabTreeStore::Result::kOk) {
      return result;
    }
  }
  return view_model_.SetExpanded(node_id, true)
             ? tab_tree::TabTreeStore::Result::kOk
             : tab_tree::TabTreeStore::Result::kInvalidArgument;
}

bool SidebarTreeController::CollapseNode(const base::Uuid& node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return view_model_.SetExpanded(node_id, false);
}

bool SidebarTreeController::SelectNode(std::optional<base::Uuid> node_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return view_model_.SetSelectedNode(std::move(node_id));
}

tab_tree::TabTreeStore::Result SidebarTreeController::RenameNode(
    const base::Uuid& node_id,
    std::u16string title,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return store_->RenameNode(node_id, std::move(title), modified_at);
}

tab_tree::TabTreeStore::Result SidebarTreeController::UpdateFolderPresentation(
    const base::Uuid& node_id,
    std::u16string title,
    std::u16string icon,
    std::optional<uint32_t> accent_argb,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return store_->UpdateFolderPresentation(
      node_id, std::move(title), std::move(icon), accent_argb, modified_at);
}

tab_tree::TabTreeStore::Result SidebarTreeController::CreateGroupAroundNode(
    const base::Uuid& source_node_id,
    std::u16string title,
    base::Time modified_at,
    base::Uuid* folder_id) {
  return CreateGroupAroundNodes({source_node_id}, std::move(title), modified_at,
                                folder_id);
}

tab_tree::TabTreeStore::Result SidebarTreeController::CreateGroupAroundNodes(
    const std::vector<base::Uuid>& source_node_ids,
    std::u16string title,
    base::Time modified_at,
    base::Uuid* folder_id,
    std::u16string icon,
    std::optional<uint32_t> accent_argb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return store_->CreateStyledFolderAroundNodes(
      source_node_ids, std::move(title), std::move(icon), accent_argb,
      modified_at, folder_id);
}

tab_tree::TabTreeStore::Result SidebarTreeController::CreateFolder(
    std::optional<base::Uuid> parent_id,
    std::u16string title,
    base::Time modified_at,
    base::Uuid* folder_id,
    std::u16string icon,
    std::optional<uint32_t> accent_argb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!view_model_.workspace_id().has_value() || title.empty() ||
      modified_at.is_null() || !folder_id ||
      (parent_id.has_value() && !parent_id->is_valid())) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  if (parent_id.has_value()) {
    tab_tree::TreeNode parent;
    const tab_tree::TabTreeStore::Result parent_result =
        store_->GetNode(*parent_id, &parent);
    if (parent_result != tab_tree::TabTreeStore::Result::kOk) {
      return parent_result;
    }
    if (parent.tombstone || parent.type != tab_tree::TreeNodeType::kFolder ||
        parent.workspace_id != *view_model_.workspace_id()) {
      return tab_tree::TabTreeStore::Result::kInvalidArgument;
    }
  }

  std::vector<tab_tree::TreeNode> children;
  const tab_tree::TabTreeStore::Result children_result =
      store_->GetChildren(*view_model_.workspace_id(), parent_id, &children);
  if (children_result != tab_tree::TabTreeStore::Result::kOk) {
    return children_result;
  }
  const std::optional<std::string> left =
      children.empty() ? std::nullopt
                       : std::make_optional(children.back().sort_key);
  const std::optional<std::string> sort_key =
      GenerateSortKeyBetween(left, std::nullopt);
  if (!sort_key.has_value()) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }

  tab_tree::TreeNode folder{
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = *view_model_.workspace_id(),
      .parent_id = parent_id,
      .type = tab_tree::TreeNodeType::kFolder,
      .title = std::move(title),
      .icon = std::move(icon),
      .accent_argb = accent_argb,
      .sort_key = *sort_key,
      .created_at = modified_at,
      .modified_at = modified_at,
  };
  const tab_tree::TabTreeStore::Result result = store_->CreateNode(folder);
  if (result == tab_tree::TabTreeStore::Result::kOk) {
    *folder_id = folder.id;
  }
  return result;
}

tab_tree::TabTreeStore::Result SidebarTreeController::DeleteNode(
    const base::Uuid& node_id,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return store_->DeleteNode(node_id, modified_at);
}

tab_tree::TabTreeStore::Result SidebarTreeController::DeleteNodes(
    const std::vector<base::Uuid>& node_ids,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return node_ids.size() == 1
             ? store_->DeleteNode(node_ids.front(), modified_at)
             : store_->DeleteNodesAtomically(node_ids, modified_at);
}

tab_tree::TabTreeStore::Result SidebarTreeController::UndoLastMutation() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return store_->UndoLastMutation();
}

SidebarTreeController::DropValidationResult
SidebarTreeController::ValidateNewSavedPageDrop(const DropTarget& target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!target.workspace_id.is_valid() ||
      (target.target_node_id.has_value() &&
       !target.target_node_id->is_valid()) ||
      (!target.target_node_id.has_value() &&
       target.position != DropPosition::kInside)) {
    return DropValidationResult::kInvalidArgument;
  }
  tab_tree::TreeNode synthetic_source{
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = target.workspace_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = u"Temporary tab",
      .url = GURL("about:blank"),
      .sort_key = "temporary",
      .created_at = base::Time::Now(),
      .modified_at = base::Time::Now(),
  };
  DropPlan plan;
  return ResolveDropDestination(synthetic_source, target, DropOperation::kCopy,
                                &plan);
}

tab_tree::TabTreeStore::Result SidebarTreeController::CreateSavedPageAtDrop(
    const DropTarget& target,
    std::u16string title,
    const GURL& url,
    base::Time modified_at,
    tab_tree::TreeNode* created_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (title.empty() || !url.is_valid() || url.is_empty() ||
      modified_at.is_null() || !created_node) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  tab_tree::TreeNode node{
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = target.workspace_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = std::move(title),
      .url = url,
      .sort_key = "pending",
      .created_at = modified_at,
      .modified_at = modified_at,
  };
  DropPlan plan;
  const DropValidationResult validation =
      ResolveDropDestination(node, target, DropOperation::kCopy, &plan);
  if (validation != DropValidationResult::kAllowed) {
    return StoreResultForDropValidation(validation);
  }
  node.workspace_id = plan.workspace_id;
  node.parent_id = plan.parent_id;
  node.sort_key = std::move(plan.sort_key);
  const tab_tree::TabTreeStore::Result result = store_->CreateNode(node);
  if (result == tab_tree::TabTreeStore::Result::kOk) {
    *created_node = std::move(node);
  }
  return result;
}

SidebarTreeController::DropValidationResult SidebarTreeController::ValidateDrop(
    const base::Uuid& source_node_id,
    const DropTarget& target,
    DropOperation operation,
    DropPlan* plan) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!source_node_id.is_valid() || !target.workspace_id.is_valid() || !plan ||
      (target.target_node_id.has_value() &&
       !target.target_node_id->is_valid()) ||
      (!target.target_node_id.has_value() &&
       target.position != DropPosition::kInside)) {
    return DropValidationResult::kInvalidArgument;
  }

  tab_tree::TreeNode source;
  tab_tree::TabTreeStore::Result source_result =
      tab_tree::TabTreeStore::Result::kNotFound;
  if (const tab_tree::TreeNode* cached = view_model_.GetNode(source_node_id)) {
    source = *cached;
    source_result = tab_tree::TabTreeStore::Result::kOk;
  }
  if (source_result == tab_tree::TabTreeStore::Result::kNotFound) {
    source_result = store_->GetNode(source_node_id, &source);
  }
  if (source_result == tab_tree::TabTreeStore::Result::kNotFound ||
      (source_result == tab_tree::TabTreeStore::Result::kOk &&
       source.tombstone)) {
    return DropValidationResult::kSourceNotFound;
  }
  if (source_result != tab_tree::TabTreeStore::Result::kOk) {
    return DropValidationResult::kStoreError;
  }
  return ResolveDropDestination(source, target, operation, plan);
}

SidebarTreeController::DropExecutionResult SidebarTreeController::PerformDrop(
    const base::Uuid& source_node_id,
    const DropTarget& target,
    DropOperation operation,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DropExecutionResult execution;
  if (modified_at.is_null()) {
    execution.validation = DropValidationResult::kInvalidArgument;
    return execution;
  }
  DropPlan plan;
  execution.validation = ValidateDrop(source_node_id, target, operation, &plan);
  if (execution.validation != DropValidationResult::kAllowed) {
    return execution;
  }

  if (operation == DropOperation::kMove) {
    execution.store_result =
        store_->MoveNode(source_node_id, plan.workspace_id, plan.parent_id,
                         plan.sort_key, modified_at);
    return execution;
  }

  base::Uuid copied_root_id;
  execution.store_result = CopySubtree(plan, modified_at, &copied_root_id);
  if (execution.store_result == tab_tree::TabTreeStore::Result::kOk) {
    execution.copied_root_id = copied_root_id;
  }
  return execution;
}

SidebarTreeController::DropExecutionResult
SidebarTreeController::PerformGroupedDrop(
    const std::vector<base::Uuid>& source_node_ids,
    const DropTarget& target,
    DropOperation operation,
    base::Time modified_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DropExecutionResult execution;
  if (source_node_ids.empty() || modified_at.is_null()) {
    execution.validation = DropValidationResult::kInvalidArgument;
    return execution;
  }
  if (source_node_ids.size() == 1) {
    return PerformDrop(source_node_ids.front(), target, operation, modified_at);
  }
  if (operation != DropOperation::kMove) {
    execution.validation = DropValidationResult::kInvalidArgument;
    return execution;
  }

  std::unordered_set<base::Uuid, base::UuidHash> source_ids;
  source_ids.reserve(source_node_ids.size());
  for (const base::Uuid& source_node_id : source_node_ids) {
    if (!source_node_id.is_valid() ||
        !source_ids.insert(source_node_id).second) {
      execution.validation = DropValidationResult::kInvalidArgument;
      return execution;
    }
  }
  if (target.target_node_id.has_value() &&
      source_ids.contains(*target.target_node_id)) {
    execution.validation = DropValidationResult::kNoOp;
    return execution;
  }

  DropPlan destination_plan;
  execution.validation = DropValidationResult::kNoOp;
  for (const base::Uuid& source_node_id : source_node_ids) {
    DropPlan candidate;
    const DropValidationResult validation =
        ValidateDrop(source_node_id, target, operation, &candidate);
    if (validation == DropValidationResult::kAllowed) {
      destination_plan = std::move(candidate);
      execution.validation = DropValidationResult::kAllowed;
      break;
    }
    if (validation != DropValidationResult::kNoOp) {
      execution.validation = validation;
      return execution;
    }
  }
  if (execution.validation != DropValidationResult::kAllowed) {
    return execution;
  }

  std::vector<tab_tree::TreeNode> siblings;
  const tab_tree::TabTreeStore::Result children_result = store_->GetChildren(
      destination_plan.workspace_id, destination_plan.parent_id, &siblings);
  if (children_result != tab_tree::TabTreeStore::Result::kOk) {
    execution.validation = DropValidationResult::kStoreError;
    execution.store_result = children_result;
    return execution;
  }
  std::erase_if(siblings, [&source_ids](const tab_tree::TreeNode& sibling) {
    return source_ids.contains(sibling.id);
  });

  size_t insertion_index = siblings.size();
  if (target.target_node_id.has_value() &&
      target.position != DropPosition::kInside) {
    auto target_it = std::ranges::find(siblings, *target.target_node_id,
                                       &tab_tree::TreeNode::id);
    if (target_it == siblings.end()) {
      execution.validation = DropValidationResult::kTargetNotFound;
      return execution;
    }
    insertion_index =
        static_cast<size_t>(std::distance(siblings.begin(), target_it));
    if (target.position == DropPosition::kAfter) {
      ++insertion_index;
    }
  }

  std::optional<std::string> left;
  std::optional<std::string> right;
  if (insertion_index > 0) {
    left = siblings[insertion_index - 1].sort_key;
  }
  if (insertion_index < siblings.size()) {
    right = siblings[insertion_index].sort_key;
  }

  std::vector<tab_tree::TabTreeStore::SavedPageMove> moves;
  moves.reserve(source_node_ids.size());
  for (const base::Uuid& source_node_id : source_node_ids) {
    std::optional<std::string> sort_key = GenerateSortKeyBetween(left, right);
    if (!sort_key.has_value()) {
      execution.validation = DropValidationResult::kNoOrderingSpace;
      return execution;
    }
    moves.push_back({.node_id = source_node_id,
                     .workspace_id = destination_plan.workspace_id,
                     .parent_id = destination_plan.parent_id,
                     .sort_key = *sort_key});
    left = std::move(sort_key);
  }

  execution.store_result = store_->MoveSavedPagesAtomically(moves, modified_at);
  return execution;
}

void SidebarTreeController::OnTabTreeChanged(
    const tab_tree::TabTreeChange& change) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!view_model_.workspace_id().has_value() || change.node_ids.empty() ||
      !change.subject_node_id.is_valid()) {
    return;
  }
  const base::Uuid active_workspace = *view_model_.workspace_id();

  if (change.kind == tab_tree::MutationKind::kRenamed &&
      change.node_ids.size() == 1) {
    tab_tree::TreeNode node;
    if (store_->GetNode(change.subject_node_id, &node) ==
            tab_tree::TabTreeStore::Result::kOk &&
        !node.tombstone && node.workspace_id == active_workspace) {
      (void)view_model_.CacheNode(node);
    }
    return;
  }

  std::vector<std::optional<base::Uuid>> affected_parents;
  for (const base::Uuid& node_id : change.node_ids) {
    if (const tab_tree::TreeNode* cached = view_model_.GetNode(node_id);
        cached && cached->workspace_id == active_workspace) {
      AddUniqueParent(&affected_parents, cached->parent_id);
    }
  }

  std::vector<tab_tree::TreeNode> current_active_nodes;
  std::vector<base::Uuid> inactive_node_ids;
  current_active_nodes.reserve(change.node_ids.size());
  inactive_node_ids.reserve(change.node_ids.size());
  for (const base::Uuid& node_id : change.node_ids) {
    tab_tree::TreeNode current;
    const tab_tree::TabTreeStore::Result current_result =
        store_->GetNode(node_id, &current);
    if (current_result != tab_tree::TabTreeStore::Result::kOk &&
        current_result != tab_tree::TabTreeStore::Result::kNotFound) {
      return;
    }
    if (current_result == tab_tree::TabTreeStore::Result::kOk &&
        !current.tombstone && current.workspace_id == active_workspace) {
      AddUniqueParent(&affected_parents, current.parent_id);
      current_active_nodes.push_back(std::move(current));
    } else {
      inactive_node_ids.push_back(node_id);
    }
  }

  view_model_.BeginUpdate();
  for (const tab_tree::TreeNode& current : current_active_nodes) {
    (void)view_model_.CacheNode(current);
  }
  for (const std::optional<base::Uuid>& parent_id : affected_parents) {
    if (view_model_.AreChildrenLoaded(parent_id)) {
      const tab_tree::TabTreeStore::Result result = RefreshChildren(parent_id);
      if (result != tab_tree::TabTreeStore::Result::kOk &&
          result != tab_tree::TabTreeStore::Result::kNotFound) {
        view_model_.EndUpdate();
        return;
      }
    }
  }
  view_model_.EndUpdate();
  if (!inactive_node_ids.empty()) {
    view_model_.EraseCachedNodes(inactive_node_ids);
  }
}

tab_tree::TabTreeStore::Result SidebarTreeController::RefreshChildren(
    std::optional<base::Uuid> parent_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!view_model_.workspace_id().has_value()) {
    return tab_tree::TabTreeStore::Result::kNotInitialized;
  }
  std::vector<tab_tree::TreeNode> children;
  const tab_tree::TabTreeStore::Result result =
      store_->GetChildren(*view_model_.workspace_id(), parent_id, &children);
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }
  return view_model_.ReplaceChildren(parent_id, std::move(children))
             ? tab_tree::TabTreeStore::Result::kOk
             : tab_tree::TabTreeStore::Result::kInvalidArgument;
}

}  // namespace ahoi::sidebar
