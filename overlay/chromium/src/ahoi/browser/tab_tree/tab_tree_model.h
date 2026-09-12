// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_TAB_TREE_TAB_TREE_MODEL_H_
#define AHOI_BROWSER_TAB_TREE_TAB_TREE_MODEL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/sync/shared_tab_target_types.h"
#include "ahoi/browser/sync/shared_workspace_structure_types.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::tab_tree {

inline constexpr int kCurrentModelVersion = 1;

enum class TreeNodeType {
  kFolder = 0,
  kSavedPage = 1,
};

// Persisted, profile-scoped workspace metadata. Logical identity is independent
// of Chromium Profiles and each tab's local website-session binding. No cookie,
// permission, credential or storage-partition identity belongs in these rows.
struct Workspace {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  std::u16string name;
  std::u16string icon;
  std::string sort_key;
  std::optional<uint32_t> accent_argb;
  base::Time created_at;
  base::Time modified_at;
  bool tombstone = false;

  sync::SharedArchivePolicy archive_policy = sync::SharedArchivePolicy::kNever;

  bool operator==(const Workspace&) const = default;
};

// A persistent item in the Ahoi sidebar. `parent_id == std::nullopt` denotes a
// workspace root. Parent links are unrestricted in depth but must point to an
// active folder in the same workspace.
struct TreeNode {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid workspace_id;
  std::optional<base::Uuid> parent_id;
  TreeNodeType type = TreeNodeType::kFolder;
  std::u16string title;
  // Semantic presentation identifier (for example "folder", "code" or
  // "lock") and an optional ARGB accent. Saved pages deliberately keep both
  // empty because their visual identity comes from the page favicon.
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
  GURL url;
  std::string sort_key;
  base::Time created_at;
  base::Time modified_at;
  bool tombstone = false;

  // Both saved and temporary normal pages have a stable global node ID.
  // kSavedPage is the historical page-kind name; this flag is authoritative
  // for saved/temporary presentation and close semantics.
  bool is_temporary = false;
  // Native legacy pages may omit this metadata and are classified from their
  // local URL at the boundary. Shared page projections always supply it.
  // For local-only targets url may retain this device's actual target; those
  // bytes NEVER enter the portable URL field or another device's navigation.
  std::optional<sync::SharedTabTargetKind> target_kind;
  std::optional<std::string> local_scheme;

  // Saved Home is independent of the current navigation URL. A local-only
  // destination remains private here; only its portable descriptor is shared.
  // A concurrent Unsave may retain dormant Home metadata on a temporary page.
  GURL home_url;
  std::optional<sync::SharedTabTargetKind> home_target_kind;
  std::optional<std::string> home_local_scheme;

  bool operator==(const TreeNode&) const = default;
};

// Only this bounded target leaves the device. A local-only native URL may be
// retained in the row for local use, but is never returned by this boundary.
std::optional<sync::SharedTabTarget> GetSharedPageTarget(const TreeNode& node);
std::optional<sync::SharedTabTarget> GetSharedHomeTarget(const TreeNode& node);
// Local creation/save only. Never call while applying a shared snapshot or
// observing navigation: an absent incoming Home is an intentional value.
void InitializeSavedHome(TreeNode* node);
// Native local-only captions may contain file paths or code-derived text.
// Preserve their local display but use a generic portable caption on the wire.
std::u16string GetSharedPageTitle(const TreeNode& node);

// Complete persistence snapshot for transferring the live in-memory tree to
// Ahoi's dedicated blocking database sequence. Undo history is part of the
// snapshot so moving SQLite off the UI sequence does not weaken restart or
// undo semantics.
enum class UndoMutationKind {
  kCreate = 0,
  kRename = 1,
  kMove = 2,
  kDelete = 3,
};

struct UndoNodeSnapshot {
  base::Uuid node_id;
  std::optional<TreeNode> previous;

  bool operator==(const UndoNodeSnapshot&) const = default;
};

struct UndoOperationSnapshot {
  int64_t operation_id = 0;
  UndoMutationKind kind = UndoMutationKind::kCreate;
  base::Uuid subject_node_id;
  base::Time created_at;
  std::vector<UndoNodeSnapshot> nodes;

  bool operator==(const UndoOperationSnapshot&) const = default;
};

struct TabTreeSnapshot {
  std::vector<Workspace> workspaces;
  std::vector<TreeNode> nodes;
  std::vector<UndoOperationSnapshot> undo_operations;

  bool operator==(const TabTreeSnapshot&) const = default;
};

enum class MutationKind {
  kCreated = 0,
  kRenamed = 1,
  kMoved = 2,
  kDeleted = 3,
  kUndone = 4,
};

// Commit-level invalidation seam for a future native Views sidebar. Consumers
// re-read changed rows from TabTreeStore instead of receiving mutable objects.
struct TabTreeChange {
  MutationKind kind = MutationKind::kCreated;
  // Primary mutation root. For subtree operations this lets native consumers
  // refresh affected parent lists without re-reading every descendant.
  base::Uuid subject_node_id;
  std::vector<base::Uuid> node_ids;
};

}  // namespace ahoi::tab_tree

#endif  // AHOI_BROWSER_TAB_TREE_TAB_TREE_MODEL_H_
