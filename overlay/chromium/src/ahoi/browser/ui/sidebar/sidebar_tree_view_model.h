// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_MODEL_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_MODEL_H_

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/uuid.h"

namespace ahoi::sidebar {

// Row-oriented delta contract for a virtualized native Views consumer. M151's
// generic views::TreeView walks the hierarchy to derive rows and explicitly
// does not scale well; this model keeps the visible projection authoritative.
class SidebarTreeViewModelObserver : public base::CheckedObserver {
 public:
  virtual void OnBatchUpdateStarted() {}
  virtual void OnBatchUpdateEnded() {}
  virtual void OnTreeReset() = 0;
  // Emitted immediately before a nonempty expansion splice. It carries intent
  // only; observers must not mutate the model or delay its authoritative delta.
  virtual void OnFolderExpansionChanging(const base::Uuid& node_id,
                                         bool expanded) {}
  virtual void OnRowsInserted(size_t first_row, size_t count) = 0;
  virtual void OnRowsRemoved(size_t first_row, size_t count) = 0;
  virtual void OnRowsChanged(size_t first_row, size_t count) = 0;
  virtual void OnSelectionChanged(
      const std::optional<base::Uuid>& old_selection,
      const std::optional<base::Uuid>& new_selection) = 0;

 protected:
  ~SidebarTreeViewModelObserver() override = default;
};

class SidebarTreeViewModel {
 public:
  struct Row {
    base::Uuid node_id;
    size_t depth = 0;
    // One-based sibling position and sibling count are part of the flattened
    // projection so a virtualized accessibility consumer can expose ARIA tree
    // metadata without walking a potentially 10,000-node sibling list for
    // every materialized row.
    size_t position_in_parent = 0;
    size_t sibling_count = 0;
    tab_tree::TreeNodeType type = tab_tree::TreeNodeType::kFolder;
    bool expanded = false;

    bool operator==(const Row&) const = default;
  };

  SidebarTreeViewModel();
  SidebarTreeViewModel(const SidebarTreeViewModel&) = delete;
  SidebarTreeViewModel& operator=(const SidebarTreeViewModel&) = delete;
  ~SidebarTreeViewModel();

  void AddObserver(SidebarTreeViewModelObserver* observer);
  void RemoveObserver(SidebarTreeViewModelObserver* observer);

  // Clears the loaded projection and selects a new persistence root. A full
  // reset is intentionally reserved for this explicit workspace transition.
  [[nodiscard]] bool ResetWorkspace(const base::Uuid& workspace_id);

  // Replaces one loaded child list. The visible splice is calculated only when
  // the parent is expanded (or is the implicit workspace root).
  [[nodiscard]] bool ReplaceChildren(std::optional<base::Uuid> parent_id,
                                     std::vector<tab_tree::TreeNode> children);

  // Updates immutable data already cached for a node. Structural changes are
  // accepted for a later ReplaceChildren() splice but do not emit a row change.
  [[nodiscard]] bool CacheNode(const tab_tree::TreeNode& node);
  void EraseCachedNodes(const std::vector<base::Uuid>& node_ids);

  [[nodiscard]] bool SetExpanded(const base::Uuid& node_id, bool expanded);
  [[nodiscard]] bool SetSelectedNode(std::optional<base::Uuid> node_id);

  // Replaces the ordinary expansion-driven rows with a transient search
  // projection. Only exact matches, their complete loaded ancestor chains and
  // loaded members of a matching split group are shown. The durable tree,
  // expansion set and selection are deliberately not changed. An empty set is
  // a valid active projection with no rows.
  [[nodiscard]] bool SetSearchMatches(
      std::unordered_set<base::Uuid, base::UuidHash> node_ids);
  void ClearSearchMatches();
  // Split membership belongs to the live browser presentation rather than the
  // persisted tab tree. The controller hydrates missing group members before
  // updating this context while search is active; direct callers must only use
  // this setter when no hydration is required.
  void SetSearchContextGroups(
      std::vector<std::vector<base::Uuid>> context_groups);

  void BeginUpdate();
  void EndUpdate();
  void NormalizeSelection();

  const std::optional<base::Uuid>& workspace_id() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return workspace_id_;
  }
  const std::vector<Row>& rows() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return rows_;
  }
  const std::optional<base::Uuid>& selected_node_id() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return selected_node_id_;
  }

  const tab_tree::TreeNode* GetNode(const base::Uuid& node_id) const;
  std::optional<size_t> GetRowForNode(const base::Uuid& node_id) const;
  bool IsExpanded(const base::Uuid& node_id) const;
  bool is_search_projection_active() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return search_exact_match_node_ids_.has_value();
  }
  // Exact matches originate in the search result set. Context rows are the
  // ancestors and saved split partners that keep those matches intelligible
  // inside the hierarchy. IsSearchMatch() remains as the compatibility name
  // for callers that only need exact-match semantics.
  bool IsSearchExactMatch(const base::Uuid& node_id) const;
  bool IsSearchContext(const base::Uuid& node_id) const;
  bool IsSearchMatch(const base::Uuid& node_id) const;
  std::vector<base::Uuid> GetSearchExactMatches() const;
  const std::vector<std::vector<base::Uuid>>& search_context_groups() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return search_context_groups_;
  }
  bool AreChildrenLoaded(std::optional<base::Uuid> parent_id) const;
  // Returns transient pointers for same-sequence validation; callers must not
  // retain them across any model mutation.
  [[nodiscard]] bool GetLoadedChildren(
      std::optional<base::Uuid> parent_id,
      std::vector<const tab_tree::TreeNode*>* children) const;
  size_t cached_node_count_for_testing() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return nodes_.size();
  }

 private:
  struct CachedNode {
    tab_tree::TreeNode node;
    bool children_loaded = false;
    std::vector<base::Uuid> children;
  };

  std::vector<Row> BuildVisibleChildren(
      const std::vector<base::Uuid>& child_ids,
      size_t depth) const;
  void IncludeLoadedSearchChain(
      const base::Uuid& leaf_id,
      std::unordered_set<base::Uuid, base::UuidHash>* included) const;
  std::vector<Row> BuildSearchProjection() const;
  void RebuildCurrentProjection(bool preserve_selection);
  void ApplyVisibleSplice(size_t first_row,
                          size_t old_count,
                          std::vector<Row> replacement,
                          bool preserve_selection = false);
  void RebuildRowIndex(size_t first_row);
  void NotifyChangedRuns(size_t first_row,
                         const std::vector<Row>& old_rows,
                         const std::vector<Row>& new_rows,
                         size_t prefix_count,
                         size_t suffix_count);
  void NotifyRowsChanged(size_t first_row, size_t count);
  bool IsRowVisible(const base::Uuid& node_id) const;

  std::optional<base::Uuid> workspace_id_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::unordered_map<base::Uuid, CachedNode, base::UuidHash> nodes_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::vector<base::Uuid> root_children_ GUARDED_BY_CONTEXT(sequence_checker_);
  bool root_children_loaded_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  std::unordered_set<base::Uuid, base::UuidHash> expanded_nodes_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // std::nullopt means ordinary expansion-driven rows; an engaged empty set
  // means an active search with zero saved-tree matches.
  std::optional<std::unordered_set<base::Uuid, base::UuidHash>>
      search_exact_match_node_ids_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::vector<std::vector<base::Uuid>> search_context_groups_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::vector<Row> rows_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::unordered_map<base::Uuid, size_t, base::UuidHash> row_by_id_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::optional<base::Uuid> selected_node_id_
      GUARDED_BY_CONTEXT(sequence_checker_);
  size_t update_depth_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  base::ObserverList<SidebarTreeViewModelObserver> observers_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_TREE_VIEW_MODEL_H_
