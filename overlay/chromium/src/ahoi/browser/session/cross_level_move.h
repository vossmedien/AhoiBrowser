// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_CROSS_LEVEL_MOVE_H_
#define AHOI_BROWSER_SESSION_CROSS_LEVEL_MOVE_H_

#include <map>
#include <optional>
#include <vector>

#include "ahoi/browser/session/portable_workspace_structure.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace ahoi::session {

// ADR 0011 WS-ISO-05: moving a tab, folder or split between Workspaces of
// different Chromium Profiles ("Vollständig getrennt"). A WebContents never
// changes Profile: the item's structure moves through the portable format,
// its open pages are reopened by URL in the target, and sign-ins, cookies
// and site data stay behind.

enum class CrossLevelMoveCheck {
  kOk,
  // Unknown, deleted or archived roots, or overlapping roots.
  kNothingToMove,
  // A split has members inside and outside the item. It moves as a whole or
  // not at all, so it never spans two Profiles.
  kSplitWouldMix,
  // A page that only opens on this side (browser pages, files, an empty new
  // tab). Moving it would lose it.
  kLocalOnlyPage,
};

// The moved item detached from the source Profile, still in source ids.
struct CrossLevelMovePayload {
  // Disjoint roots in their display order.
  std::vector<base::Uuid> root_ids;
  // Every node of the item, roots and descendants, parent before child.
  std::vector<tab_tree::PortableWorkspaceNode> nodes;
  // Complete splits of the item only.
  std::vector<sync::SharedSplitMetadata> splits;
  size_t pages = 0;
  size_t folders = 0;
};

// Collects the roots' subtrees from the source Profile's durable state. A
// temporary page arrives as a saved page, like the same-Profile "Move to".
CrossLevelMoveCheck ExtractCrossLevelMove(
    const tab_tree::TabTreeSnapshot& tree,
    const WorkspaceStructureState& structure,
    const std::vector<base::Uuid>& root_ids,
    CrossLevelMovePayload* payload);

// Whether `node_id` is one of `root_ids` or lies inside one of them,
// walking up through `parent_of` (nullopt at a Workspace root). A move that
// takes the active tab this way, also inside a moved folder, lets the
// window follow it (handoff 011 S1).
bool CrossLevelMoveContains(
    const std::vector<base::Uuid>& root_ids,
    const base::Uuid& node_id,
    const base::RepeatingCallback<std::optional<base::Uuid>(
        const base::Uuid&)>& parent_of);

struct CrossLevelMovePlacement {
  // The target Workspace's own current record plus only new nodes and
  // splits, so the portable import is purely additive.
  PortableWorkspaceStructure import;
  // Source node id -> target node id.
  std::map<base::Uuid, base::Uuid> new_ids;
  std::vector<base::Uuid> root_ids;
};

// Gives every node and split a fresh identity (the same ids must never exist
// in two Profiles' Sync namespaces) and places the roots after the target's
// last root, keeping their order. nullopt for an unknown target.
std::optional<CrossLevelMovePlacement> PlaceCrossLevelMove(
    const CrossLevelMovePayload& payload,
    const tab_tree::TabTreeSnapshot& target_tree,
    const base::Uuid& target_workspace_id,
    const base::RepeatingCallback<base::Uuid()>& make_id);

// One finished move, remembered in memory for ⌘Z like a merge receipt; it
// does not survive a restart.
struct CrossLevelMoveReceipt {
  base::FilePath source_profile_path;
  base::FilePath target_profile_path;
  base::Uuid source_workspace_id;
  base::Uuid target_workspace_id;
  // Subject of the source's undoable deletion; empty when only a temporary
  // tab moved, which closes without an undo entry.
  std::optional<base::Uuid> source_deletion_subject;
  // Saved source pages that were open, reopened again by an undo.
  std::vector<base::Uuid> source_open_ids;
  // Open temporary pages of the source, reopened by URL on undo.
  std::vector<GURL> source_temporary_urls;
  std::vector<base::Uuid> target_root_ids;
  std::vector<base::Uuid> target_node_ids;
  base::Time moved_at;
};

void RememberCrossLevelMove(CrossLevelMoveReceipt receipt);
const CrossLevelMoveReceipt* GetLatestCrossLevelMove();
void ForgetCrossLevelMove();

// The most recent entry of one Profile's tree undo history.
struct LatestTreeUndo {
  tab_tree::UndoMutationKind kind = tab_tree::UndoMutationKind::kCreate;
  base::Uuid subject_node_id;
  base::Time created_at;
};
std::optional<LatestTreeUndo> FindLatestTreeUndo(
    const tab_tree::TabTreeSnapshot& tree);

// Whether the move is still this side's most recent structural change, so
// ⌘Z there undoes the move and not an older or newer local change. The
// source side matches its own deletion; the target has no undo entry for
// the imported copy, so it only requires that nothing newer happened there.
bool IsCrossLevelMoveLatestOnSource(
    const CrossLevelMoveReceipt& receipt,
    const std::optional<LatestTreeUndo>& latest);
bool IsCrossLevelMoveLatestOnTarget(
    const CrossLevelMoveReceipt& receipt,
    const std::optional<LatestTreeUndo>& latest);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_CROSS_LEVEL_MOVE_H_
