// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-05: moving a tab, folder or split between Workspaces of
// different Profiles. The source asks and later closes its open pages, the
// target imports the structure with fresh ids and reopens pages by URL. A
// WebContents never changes Profile; sign-ins and site data stay behind.

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/session/cross_level_move.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_internal.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi {

namespace {

// Like a converted Workspace (handoff 052), the receiving structure
// controller may still be settling its first commit; retry briefly.
constexpr int kCrossLevelImportAttempts = 40;
constexpr base::TimeDelta kCrossLevelImportRetry = base::Milliseconds(250);

using PlacementCallback =
    base::OnceCallback<void(std::optional<session::CrossLevelMovePlacement>)>;

void RetryCrossLevelImport(base::WeakPtr<SessionBridge> bridge,
                           session::CrossLevelMovePayload payload,
                           base::Uuid workspace_id,
                           PlacementCallback done,
                           int attempt) {
  if (!bridge || attempt >= kCrossLevelImportAttempts) {
    std::move(done).Run(std::nullopt);
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SessionBridge::ImportCrossLevelMove, bridge,
                     std::move(payload), workspace_id, std::move(done),
                     attempt),
      kCrossLevelImportRetry);
}

}  // namespace

session::CrossLevelMoveCheck SessionBridge::CheckCrossLevelMove(
    const std::vector<base::Uuid>& root_ids,
    session::CrossLevelMovePayload* payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::TabTreeSnapshot snapshot;
  const std::optional<std::string> encoded =
      is_ready() ? tab_tree_store_->ReadWorkspaceStructureState()
                 : std::nullopt;
  if (!encoded || !ExportTabTreeSnapshot(&snapshot)) {
    return session::CrossLevelMoveCheck::kNothingToMove;
  }
  session::WorkspaceStructureState structure;
  if (!encoded->empty()) {
    std::optional<session::WorkspaceStructureState> decoded =
        session::DecodeWorkspaceStructureState(*encoded);
    if (!decoded) {
      return session::CrossLevelMoveCheck::kNothingToMove;
    }
    structure = std::move(*decoded);
  }
  return session::ExtractCrossLevelMove(snapshot, structure, root_ids,
                                        payload);
}

void SessionBridge::AskCrossLevelMovePages(
    const std::vector<base::Uuid>& root_ids,
    base::OnceCallback<void(std::optional<CrossLevelOpenPages>)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::set<base::Uuid> moved;
  for (const base::Uuid& root : root_ids) {
    std::vector<tab_tree::TreeNode> subtree;
    if (tab_tree_store_ &&
        tab_tree_store_->GetSubtree(root, &subtree) ==
            tab_tree::TabTreeStore::Result::kOk) {
      for (const tab_tree::TreeNode& node : subtree) {
        moved.insert(node.id);
      }
    }
  }
  if (shutting_down_ || moved.empty()) {
    std::move(done).Run(std::nullopt);
    return;
  }
  // A move abandoned by a closed window leaves its question behind; its
  // pages simply stay open.
  cross_level_close_.reset();
  CrossLevelOpenPages open;
  std::vector<content::WebContents*> pages;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    content::WebContents* contents = runtime.web_contents.get();
    if (!contents || !runtime.node_id || !moved.contains(*runtime.node_id)) {
      continue;
    }
    pages.push_back(contents);
    open.open_ids.push_back(*runtime.node_id);
    if (!runtime.is_temporary) {
      open.saved_ids.push_back(*runtime.node_id);
      continue;
    }
    const GURL url = session_internal::GetRuntimeTabUrl(contents);
    if (url.is_valid() && !url.IsAboutBlank()) {
      open.temporary_urls.push_back(url);
    }
  }
  auto answered = base::BindOnce(
      [](base::WeakPtr<SessionBridge> bridge, CrossLevelOpenPages open,
         base::OnceCallback<void(std::optional<CrossLevelOpenPages>)> done,
         bool all_agreed) {
        if (!bridge || !all_agreed) {
          // A veto leaves every page and both trees unchanged.
          if (bridge) {
            bridge->cross_level_close_.reset();
          }
          std::move(done).Run(std::nullopt);
          return;
        }
        std::move(done).Run(std::move(open));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(open), std::move(done));
  cross_level_close_ =
      session::GroupPageClose::Ask(std::move(pages), std::move(answered));
}

std::optional<base::Uuid> SessionBridge::FinishCrossLevelMoveSource(
    const std::vector<base::Uuid>& root_ids,
    bool commit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unique_ptr<session::GroupPageClose> group =
      std::move(cross_level_close_);
  if (!commit || shutting_down_ || !tab_tree_store_) {
    return std::nullopt;
  }
  // Saved rows leave in one undo entry. An open temporary page has no row
  // to keep: closing its tab removes it, as for an explicit close.
  std::vector<base::Uuid> saved_roots;
  for (const base::Uuid& root : root_ids) {
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(root, &node) ==
            tab_tree::TabTreeStore::Result::kOk &&
        !node.tombstone && !node.is_temporary) {
      saved_roots.push_back(root);
    }
  }
  std::optional<base::Uuid> subject;
  if (!saved_roots.empty()) {
    const tab_tree::TabTreeStore::Result result =
        tab_tree_store_->DeleteNodesAtomically(saved_roots, base::Time::Now());
    if (result != tab_tree::TabTreeStore::Result::kOk) {
      // The copy already exists in the target; keeping the source as well
      // loses nothing and the user sees both.
      LOG(ERROR) << "Ahoi moved an item but could not remove its source: "
                 << static_cast<int>(result);
      return std::nullopt;
    }
    subject = saved_roots.front();
    ScheduleTabTreePersistence();
  }
  if (group) {
    group->ClosePages();
  }
  return subject;
}

void SessionBridge::ImportCrossLevelMove(
    session::CrossLevelMovePayload payload,
    base::Uuid workspace_id,
    PlacementCallback done,
    int attempt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::TabTreeSnapshot snapshot;
  if (shutting_down_) {
    std::move(done).Run(std::nullopt);
    return;
  }
  if (!is_ready() || !ExportTabTreeSnapshot(&snapshot)) {
    RetryCrossLevelImport(weak_ptr_factory_.GetWeakPtr(), std::move(payload),
                          workspace_id, std::move(done), attempt + 1);
    return;
  }
  std::optional<session::CrossLevelMovePlacement> placement =
      session::PlaceCrossLevelMove(
          payload, snapshot, workspace_id,
          base::BindRepeating(&base::Uuid::GenerateRandomV4));
  if (!placement) {
    std::move(done).Run(std::nullopt);
    return;
  }
  const session::PortableWorkspaceStructure import = placement->import;
  CommitPortableWorkspaceImport(
      import, base::BindRepeating([] { return true; }),
      base::BindOnce(
          [](base::WeakPtr<SessionBridge> bridge,
             session::CrossLevelMovePayload payload, base::Uuid workspace_id,
             session::CrossLevelMovePlacement placement,
             PlacementCallback done, int attempt,
             PortableImportResult result) {
            if (result == PortableImportResult::kImported) {
              if (bridge) {
                bridge->ScheduleTabTreePersistence();
              }
              std::move(done).Run(std::move(placement));
              return;
            }
            if (result == PortableImportResult::kUnavailable) {
              RetryCrossLevelImport(bridge, std::move(payload), workspace_id,
                                    std::move(done), attempt + 1);
              return;
            }
            LOG(ERROR) << "Ahoi could not import a moved item: "
                       << static_cast<int>(result);
            std::move(done).Run(std::nullopt);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(payload), workspace_id,
          std::move(*placement), std::move(done), attempt));
}

void SessionBridge::ReopenCrossLevelPages(
    BrowserWindowInterface* window,
    const std::vector<base::Uuid>& node_ids,
    bool activate_first) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!window || !is_ready() || !windows_.contains(window)) {
    return;
  }
  const base::WeakPtr<SessionBridge> alive = weak_ptr_factory_.GetWeakPtr();
  std::vector<base::Uuid> opened;
  for (const base::Uuid& id : node_ids) {
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(id, &node) !=
            tab_tree::TabTreeStore::Result::kOk ||
        node.tombstone || node.type != tab_tree::TreeNodeType::kSavedPage ||
        FindTabByTreeNodeId(id)) {
      continue;
    }
    const std::optional<sync::SharedTabTarget> target =
        tab_tree::GetSharedPageTarget(node);
    if (!target || target->kind == sync::SharedTabTargetKind::kLocalOnly) {
      continue;
    }
    const bool foreground = activate_first && opened.empty();
    if (foreground &&
        GetActiveWorkspaceForWindow(window) != node.workspace_id) {
      std::ignore = SetActiveWorkspaceForWindow(
          window, node.workspace_id, WorkspaceActivationSource::kSidebar);
    }
    NavigateParams params(
        window,
        target->kind == sync::SharedTabTargetKind::kWeb
            ? GURL(target->url)
            : GURL(chrome::kChromeUINewTabURL),
        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = foreground ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                                    : WindowOpenDisposition::NEW_BACKGROUND_TAB;
    ::Navigate(&params);
    if (!alive) {
      return;
    }
    tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(
        params.navigated_or_inserted_contents);
    if (tab && BindTreeNodeToTab(node, tab)) {
      opened.push_back(id);
    }
  }
  // A moved split comes back as a split once its panes are open.
  for (const base::Uuid& id : opened) {
    std::ignore = MaterializeSplitForActivation(id);
  }
}

void SessionBridge::RemoveCrossLevelCopy(
    const std::vector<base::Uuid>& root_ids,
    base::OnceCallback<void(bool)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<base::Uuid> live_roots;
  std::set<base::Uuid> copy;
  for (const base::Uuid& root : root_ids) {
    tab_tree::TreeNode node;
    std::vector<tab_tree::TreeNode> subtree;
    if (!tab_tree_store_ ||
        tab_tree_store_->GetNode(root, &node) !=
            tab_tree::TabTreeStore::Result::kOk ||
        node.tombstone ||
        tab_tree_store_->GetSubtree(root, &subtree) !=
            tab_tree::TabTreeStore::Result::kOk) {
      // Already removed by the user; nothing of it to take back.
      continue;
    }
    live_roots.push_back(root);
    for (const tab_tree::TreeNode& member : subtree) {
      copy.insert(member.id);
    }
  }
  if (shutting_down_ || cross_level_copy_close_) {
    std::move(done).Run(false);
    return;
  }
  std::vector<content::WebContents*> pages;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.web_contents && runtime.node_id &&
        copy.contains(*runtime.node_id)) {
      pages.push_back(runtime.web_contents.get());
    }
  }
  cross_level_copy_close_ = session::GroupPageClose::Ask(
      std::move(pages),
      base::BindOnce(
          [](base::WeakPtr<SessionBridge> bridge,
             std::vector<base::Uuid> roots,
             base::OnceCallback<void(bool)> done, bool all_agreed) {
            if (!bridge) {
              std::move(done).Run(false);
              return;
            }
            std::unique_ptr<session::GroupPageClose> group =
                std::move(bridge->cross_level_copy_close_);
            if (!all_agreed) {
              std::move(done).Run(false);
              return;
            }
            if (!roots.empty() &&
                bridge->tab_tree_store_->DeleteNodesAtomically(
                    roots, base::Time::Now(), /*record_undo=*/false) !=
                    tab_tree::TabTreeStore::Result::kOk) {
              std::move(done).Run(false);
              return;
            }
            bridge->ScheduleTabTreePersistence();
            if (group) {
              group->ClosePages();
            }
            std::move(done).Run(true);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(live_roots),
          std::move(done)));
}

std::optional<session::LatestTreeUndo> SessionBridge::GetLatestTreeUndo() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::TabTreeSnapshot snapshot;
  if (!is_ready() || !ExportTabTreeSnapshot(&snapshot)) {
    return std::nullopt;
  }
  return session::FindLatestTreeUndo(snapshot);
}

}  // namespace ahoi
