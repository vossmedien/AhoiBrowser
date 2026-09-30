// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-10: Arc profiles mapped to fully separated Workspaces. The
// main Profile's transaction has finished (or was a verified no-op) before
// this runs; the separated Workspaces are separate Profiles and never part of
// its journal, backup or rollback.

#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_separated_workspaces.h"
#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/functional/bind.h"

namespace ahoi::importer::arc {

namespace {

void ReportSeparatedOutcome(ArcImportCommitResult result,
                            ArcImportCommitCallback callback,
                            ArcSeparatedImportOutcome outcome) {
  result.separated_workspace_count = outcome.created;
  result.existing_separated_workspace_count = outcome.existing;
  result.failed_separated_workspace_count = outcome.failed;
  if (result.status == ArcImportStatus::kNoChanges) {
    if (outcome.created > 0) {
      result.status = ArcImportStatus::kOk;
    } else if (outcome.failed > 0) {
      result.status = ArcImportStatus::kTransactionFailed;
    }
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace

void ArcImportService::FinishWithSeparatedWorkspaces(
    std::vector<ArcSeparatedWorkspacePlan> separated,
    ArcImportCommitResult result,
    ArcImportCommitCallback callback) {
  if (separated.empty()) {
    std::move(callback).Run(std::move(result));
    return;
  }
  // New separated Workspaces order after this Profile's Workspaces.
  std::vector<session::DirectoryWorkspace> main_workspaces;
  tab_tree::TabTreeSnapshot current;
  if (session_bridge_ && session_bridge_->ExportTabTreeSnapshot(&current)) {
    for (const tab_tree::Workspace& workspace : current.workspaces) {
      if (!workspace.tombstone) {
        main_workspaces.push_back({.workspace_id = workspace.id,
                                   .sort_key = workspace.sort_key});
      }
    }
  }
  // The importer lock is already released: a second commit while Profiles
  // are still being created finds their registry entries (registered
  // synchronously) and reports them as existing instead of duplicating.
  CreateArcSeparatedWorkspaces(
      std::move(separated), std::move(main_workspaces),
      base::BindOnce(&ReportSeparatedOutcome, std::move(result),
                     std::move(callback)));
}

}  // namespace ahoi::importer::arc
