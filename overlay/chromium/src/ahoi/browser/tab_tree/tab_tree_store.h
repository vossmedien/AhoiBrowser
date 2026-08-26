// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_H_
#define AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_observer.h"
#include "base/files/file_path.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "sql/database.h"

namespace sql {
class MetaTable;
}

namespace ahoi::tab_tree {

class TabTreeStore {
 public:
  static constexpr int kCurrentSchemaVersion = 2;
  static constexpr int kLowestSupportedSchemaVersion = 1;

  enum class Result {
    kOk = 0,
    kNotInitialized,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kCycle,
    kNothingToUndo,
    kDatabaseError,
  };

  struct SavedPageMove {
    base::Uuid node_id;
    base::Uuid workspace_id;
    std::optional<base::Uuid> parent_id;
    std::string sort_key;
  };

  TabTreeStore();
  TabTreeStore(const TabTreeStore&) = delete;
  TabTreeStore& operator=(const TabTreeStore&) = delete;
  ~TabTreeStore();

  // Opens (or creates) a profile-local database and atomically initializes its
  // versioned schema. All later calls must run on this same sequence.
  [[nodiscard]] bool Initialize(const base::FilePath& path);
  // Opens an in-memory database for isolated tests and transient tools.
  [[nodiscard]] bool InitializeInMemory();

  void AddObserver(TabTreeObserver* observer);
  void RemoveObserver(TabTreeObserver* observer);

  [[nodiscard]] Result CreateWorkspace(const Workspace& workspace);
  // Creates a new workspace with a deep copy of every active node in the
  // source workspace. Node identities are regenerated and parent links are
  // remapped within one SQLite transaction; the source is left untouched.
  [[nodiscard]] Result DuplicateWorkspace(const base::Uuid& source_workspace_id,
                                          const Workspace& duplicate_workspace);
  // Updates the user-facing workspace metadata without changing its stable
  // identity or manual order. Workspace rows are profile-local and are never
  // Chromium Profiles themselves.
  [[nodiscard]] Result UpdateWorkspacePresentation(
      const base::Uuid& workspace_id,
      std::u16string name,
      std::u16string icon,
      std::optional<uint32_t> accent_argb,
      base::Time modified_at);
  // Tombstones one workspace and all of its persistent tree rows in a single
  // transaction. Callers must keep at least one visible workspace and move
  // any live Chromium tabs to that fallback before presenting the result.
  [[nodiscard]] Result DeleteWorkspace(const base::Uuid& workspace_id,
                                       base::Time modified_at);
  [[nodiscard]] Result CreateNode(const TreeNode& node);
  // Atomically creates one connected tree in one durable undo operation.
  // Parents may be part of the same batch and input order is irrelevant. This
  // is the storage primitive used when the native sidebar copies a subtree.
  [[nodiscard]] Result CreateNodesAtomically(
      const std::vector<TreeNode>& nodes);
  // Atomically inserts a folder at the source node's current position and
  // reparents the source node into it. Both changes form one durable undo
  // operation so a cancelled/failed partial group can never be persisted.
  [[nodiscard]] Result CreateFolderAroundNode(const base::Uuid& source_node_id,
                                              std::u16string title,
                                              base::Time modified_at,
                                              base::Uuid* folder_id);
  // The ordered pages are wrapped in one new folder and persisted as one undo
  // operation. This is used for a Chromium split collection so no pane can be
  // left behind in the source group.
  [[nodiscard]] Result CreateFolderAroundNodes(
      const std::vector<base::Uuid>& source_node_ids,
      std::u16string title,
      base::Time modified_at,
      base::Uuid* folder_id);
  [[nodiscard]] Result CreateStyledFolderAroundNodes(
      const std::vector<base::Uuid>& source_node_ids,
      std::u16string title,
      std::u16string icon,
      std::optional<uint32_t> accent_argb,
      base::Time modified_at,
      base::Uuid* folder_id);
  [[nodiscard]] Result RenameNode(const base::Uuid& node_id,
                                  std::u16string title,
                                  base::Time modified_at);
  [[nodiscard]] Result UpdateFolderPresentation(
      const base::Uuid& node_id,
      std::u16string title,
      std::u16string icon,
      std::optional<uint32_t> accent_argb,
      base::Time modified_at);
  // Updates the live destination represented by a saved page without adding a
  // user-facing undo entry. Manual titles can be preserved by passing the
  // existing title while the URL changes.
  [[nodiscard]] Result UpdateSavedPageMetadata(const base::Uuid& node_id,
                                               std::u16string title,
                                               const GURL& url,
                                               base::Time modified_at);
  [[nodiscard]] Result MoveNode(const base::Uuid& node_id,
                                const base::Uuid& workspace_id,
                                std::optional<base::Uuid> parent_id,
                                std::string sort_key,
                                base::Time modified_at);
  // Applies every saved-page destination in one SQLite transaction and one
  // durable undo entry. Validation completes before any row is changed.
  [[nodiscard]] Result MoveSavedPagesAtomically(
      const std::vector<SavedPageMove>& moves,
      base::Time modified_at);
  [[nodiscard]] Result DeleteNode(const base::Uuid& node_id,
                                  base::Time modified_at);
  // Tombstones multiple disjoint roots (and their descendants) in one SQLite
  // transaction and one durable undo operation. This keeps every pane of a
  // split collection together when saved tabs are moved back to the temporary
  // open-tab section.
  [[nodiscard]] Result DeleteNodesAtomically(
      const std::vector<base::Uuid>& node_ids,
      base::Time modified_at);
  [[nodiscard]] Result UndoLastMutation();

  [[nodiscard]] Result GetWorkspaces(std::vector<Workspace>* workspaces);
  [[nodiscard]] Result GetWorkspace(const base::Uuid& workspace_id,
                                    Workspace* workspace);
  [[nodiscard]] Result GetNode(const base::Uuid& node_id, TreeNode* node);
  [[nodiscard]] Result GetSubtree(const base::Uuid& node_id,
                                  std::vector<TreeNode>* nodes);
  [[nodiscard]] Result GetChildren(const base::Uuid& workspace_id,
                                   std::optional<base::Uuid> parent_id,
                                   std::vector<TreeNode>* children);
  // Finds active saved pages at any nesting depth. The stable order lets a
  // restored tab strip rebind duplicate URLs one-by-one without flattening the
  // persistent tree.
  [[nodiscard]] Result FindSavedPagesByUrl(const base::Uuid& workspace_id,
                                           const GURL& url,
                                           std::vector<TreeNode>* saved_pages);

  // Copies/restores the complete database, including tombstones and durable
  // undo history. A profile bridge uses these methods on an in-memory store;
  // a dedicated MayBlock sequence owns the on-disk mirror.
  [[nodiscard]] Result ExportSnapshot(TabTreeSnapshot* snapshot);
  [[nodiscard]] Result ReplaceWithSnapshot(const TabTreeSnapshot& snapshot);

 private:
  struct NodeSnapshot {
    base::Uuid node_id;
    std::optional<TreeNode> previous;
  };

  [[nodiscard]] bool CreateSchema();
  [[nodiscard]] bool MigrateSchema(sql::MetaTable* meta_table);
  [[nodiscard]] bool InitializeSchema();
  [[nodiscard]] bool IsReady() const;
  [[nodiscard]] bool ValidateWorkspace(const Workspace& workspace) const;
  [[nodiscard]] bool ValidateNode(const TreeNode& node) const;

  [[nodiscard]] Result ReadWorkspace(const base::Uuid& workspace_id,
                                     Workspace* workspace)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] Result ReadNode(const base::Uuid& node_id, TreeNode* node)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] Result ReadSubtree(const base::Uuid& node_id,
                                   std::vector<TreeNode>* nodes)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] Result ValidateDestination(
      const base::Uuid& moving_node_id,
      const base::Uuid& workspace_id,
      const std::optional<base::Uuid>& parent_id)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool InsertUndoOperation(
      UndoMutationKind kind,
      const base::Uuid& subject_node_id,
      base::Time created_at,
      const std::vector<NodeSnapshot>& snapshots)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool RestoreSnapshot(const NodeSnapshot& snapshot)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool ReadUndoSnapshots(int64_t operation_id,
                                       std::vector<NodeSnapshot>* snapshots)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool RemoveUndoOperation(int64_t operation_id)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  void Notify(MutationKind kind,
              const base::Uuid& subject_node_id,
              std::vector<base::Uuid> node_ids)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  sql::Database db_ GUARDED_BY_CONTEXT(sequence_checker_);
  base::ObserverList<TabTreeObserver> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi::tab_tree

#endif  // AHOI_BROWSER_TAB_TREE_TAB_TREE_STORE_H_
