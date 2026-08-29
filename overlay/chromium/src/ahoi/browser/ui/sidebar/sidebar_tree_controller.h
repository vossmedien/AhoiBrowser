// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_CONTROLLER_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_CONTROLLER_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_observer.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "ahoi/browser/ui/sidebar/sidebar_tree_view_model.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::sidebar {

// Sequence-bound controller for one TabTreeStore. The store must outlive this
// object, and both must be constructed, observed, and destroyed on the store's
// owning sequence. A future visual adapter may consume the row deltas but must
// not call across sequences.
class SidebarTreeController : public tab_tree::TabTreeObserver {
 public:
  enum class DropPosition {
    kBefore = 0,
    kInside = 1,
    kAfter = 2,
  };

  enum class DropOperation {
    kMove = 0,
    kCopy = 1,
  };

  enum class DropValidationResult {
    kAllowed = 0,
    kInvalidArgument,
    kSourceNotFound,
    kTargetNotFound,
    kTargetNotFolder,
    kCycle,
    kNoOp,
    kNoOrderingSpace,
    kStoreError,
  };

  struct DropTarget {
    base::Uuid workspace_id;
    std::optional<base::Uuid> target_node_id;
    DropPosition position = DropPosition::kInside;
  };

  struct DropPlan {
    base::Uuid source_node_id;
    base::Uuid workspace_id;
    std::optional<base::Uuid> parent_id;
    size_t insertion_index = 0;
    std::string sort_key;
    DropOperation operation = DropOperation::kMove;
  };

  struct DropExecutionResult {
    DropValidationResult validation = DropValidationResult::kInvalidArgument;
    tab_tree::TabTreeStore::Result store_result =
        tab_tree::TabTreeStore::Result::kInvalidArgument;
    std::optional<base::Uuid> copied_root_id;

    bool ok() const {
      return validation == DropValidationResult::kAllowed &&
             store_result == tab_tree::TabTreeStore::Result::kOk;
    }
  };

  explicit SidebarTreeController(tab_tree::TabTreeStore* store);
  SidebarTreeController(const SidebarTreeController&) = delete;
  SidebarTreeController& operator=(const SidebarTreeController&) = delete;
  ~SidebarTreeController() override;

  SidebarTreeViewModel& view_model() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return view_model_;
  }
  const SidebarTreeViewModel& view_model() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return view_model_;
  }

  [[nodiscard]] tab_tree::TabTreeStore::Result ActivateWorkspace(
      const base::Uuid& workspace_id);
  [[nodiscard]] tab_tree::TabTreeStore::Result ExpandNode(
      const base::Uuid& node_id);
  [[nodiscard]] bool CollapseNode(const base::Uuid& node_id);
  [[nodiscard]] bool SelectNode(std::optional<base::Uuid> node_id);

  // Installs a transient, hierarchy-preserving projection for exact saved
  // tree matches in the active workspace. Parent chains and the child lists
  // that establish their real sibling order are loaded once into the existing
  // model cache. Missing or foreign-workspace IDs are ignored as stale search
  // results; malformed IDs and storage failures are reported.
  [[nodiscard]] tab_tree::TabTreeStore::Result SetSearchMatches(
      const std::vector<base::Uuid>& match_node_ids);
  // Updates live saved-split membership. If a search projection is active,
  // every partner of an exact match and its complete parent chain are loaded
  // before the projection is replaced. When search is inactive, the groups
  // are retained as lightweight presentation context without touching the
  // store.
  [[nodiscard]] tab_tree::TabTreeStore::Result SetSearchContextGroups(
      std::vector<std::vector<base::Uuid>> context_groups);
  void ClearSearchMatches();
  bool is_search_projection_active() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return view_model_.is_search_projection_active();
  }

  [[nodiscard]] tab_tree::TabTreeStore::Result RenameNode(
      const base::Uuid& node_id,
      std::u16string title,
      base::Time modified_at);
  [[nodiscard]] tab_tree::TabTreeStore::Result UpdateFolderPresentation(
      const base::Uuid& node_id,
      std::u16string title,
      std::u16string icon,
      std::optional<uint32_t> accent_argb,
      base::Time modified_at);
  [[nodiscard]] tab_tree::TabTreeStore::Result CreateGroupAroundNode(
      const base::Uuid& source_node_id,
      std::u16string title,
      base::Time modified_at,
      base::Uuid* folder_id);
  [[nodiscard]] tab_tree::TabTreeStore::Result CreateGroupAroundNodes(
      const std::vector<base::Uuid>& source_node_ids,
      std::u16string title,
      base::Time modified_at,
      base::Uuid* folder_id,
      std::u16string icon = {},
      std::optional<uint32_t> accent_argb = std::nullopt);
  [[nodiscard]] tab_tree::TabTreeStore::Result CreateFolder(
      std::optional<base::Uuid> parent_id,
      std::u16string title,
      base::Time modified_at,
      base::Uuid* folder_id,
      std::u16string icon = {},
      std::optional<uint32_t> accent_argb = std::nullopt);
  [[nodiscard]] tab_tree::TabTreeStore::Result DeleteNode(
      const base::Uuid& node_id,
      base::Time modified_at);
  [[nodiscard]] tab_tree::TabTreeStore::Result DeleteNodes(
      const std::vector<base::Uuid>& node_ids,
      base::Time modified_at);
  [[nodiscard]] tab_tree::TabTreeStore::Result UndoLastMutation();

  // Temporary Chromium tabs have no persistent source node yet. These helpers
  // validate and create a saved page at the same geometric destinations used
  // by native tree drag-and-drop without first manufacturing an orphan row.
  [[nodiscard]] DropValidationResult ValidateNewSavedPageDrop(
      const DropTarget& target);
  [[nodiscard]] tab_tree::TabTreeStore::Result CreateSavedPageAtDrop(
      const DropTarget& target,
      std::u16string title,
      const GURL& url,
      base::Time modified_at,
      tab_tree::TreeNode* created_node);

  [[nodiscard]] DropValidationResult ValidateDrop(
      const base::Uuid& source_node_id,
      const DropTarget& target,
      DropOperation operation,
      DropPlan* plan);
  [[nodiscard]] DropExecutionResult PerformDrop(
      const base::Uuid& source_node_id,
      const DropTarget& target,
      DropOperation operation,
      base::Time modified_at);
  [[nodiscard]] DropExecutionResult PerformGroupedDrop(
      const std::vector<base::Uuid>& source_node_ids,
      const DropTarget& target,
      DropOperation operation,
      base::Time modified_at);

 private:
  // tab_tree::TabTreeObserver:
  void OnTabTreeChanged(const tab_tree::TabTreeChange& change) override;

  [[nodiscard]] tab_tree::TabTreeStore::Result RefreshChildren(
      std::optional<base::Uuid> parent_id);
  [[nodiscard]] tab_tree::TabTreeStore::Result SetSearchProjection(
      const std::vector<base::Uuid>& match_node_ids,
      std::vector<std::vector<base::Uuid>> context_groups);
  [[nodiscard]] DropValidationResult ResolveDropDestination(
      const tab_tree::TreeNode& source,
      const DropTarget& target,
      DropOperation operation,
      DropPlan* plan);
  [[nodiscard]] tab_tree::TabTreeStore::Result CopySubtree(
      const DropPlan& plan,
      base::Time modified_at,
      base::Uuid* copied_root_id);
  static std::optional<std::string> GenerateSortKeyBetween(
      const std::optional<std::string>& left,
      const std::optional<std::string>& right);

  const raw_ptr<tab_tree::TabTreeStore> store_;
  SidebarTreeViewModel view_model_;
  base::ScopedObservation<tab_tree::TabTreeStore, tab_tree::TabTreeObserver>
      store_observation_{this};
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_CONTROLLER_H_
