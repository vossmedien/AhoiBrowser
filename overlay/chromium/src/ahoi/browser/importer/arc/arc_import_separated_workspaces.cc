// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_separated_workspaces.h"

#include <optional>
#include <utility>
#include <vector>

#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/portable_workspace_structure.h"
#include "base/barrier_callback.h"
#include "base/functional/bind.h"

namespace ahoi::importer::arc {

namespace {

enum class SeparatedResult {
  kCreated,
  kExisting,
  kFailed,
};

void Tally(base::OnceCallback<void(ArcSeparatedImportOutcome)> done,
           std::vector<SeparatedResult> results) {
  ArcSeparatedImportOutcome outcome;
  for (SeparatedResult result : results) {
    switch (result) {
      case SeparatedResult::kCreated:
        ++outcome.created;
        break;
      case SeparatedResult::kExisting:
        ++outcome.existing;
        break;
      case SeparatedResult::kFailed:
        ++outcome.failed;
        break;
    }
  }
  std::move(done).Run(outcome);
}

}  // namespace

void CreateArcSeparatedWorkspaces(
    std::vector<ArcSeparatedWorkspacePlan> plans,
    std::vector<session::DirectoryWorkspace> main_workspaces,
    base::OnceCallback<void(ArcSeparatedImportOutcome)> done) {
  const base::RepeatingCallback<void(SeparatedResult)> barrier =
      base::BarrierCallback<SeparatedResult>(
          plans.size(), base::BindOnce(&Tally, std::move(done)));
  for (ArcSeparatedWorkspacePlan& plan : plans) {
    std::optional<tab_tree::PortableWorkspaceSelection> tree =
        SelectArcSeparatedPortableTree(plan);
    if (!tree) {
      barrier.Run(SeparatedResult::kFailed);
      continue;
    }
    session::PendingWorkspaceConversion pending;
    pending.structure.tree = std::move(*tree);
    pending.done = base::BindOnce(
        [](base::RepeatingCallback<void(SeparatedResult)> barrier,
           bool imported) {
          barrier.Run(imported ? SeparatedResult::kCreated
                               : SeparatedResult::kFailed);
        },
        barrier);
    // Each registration is committed synchronously, so the next key already
    // orders after this Workspace.
    const session::IsolatedStructureCreation started =
        session::CreateIsolatedWorkspaceWithStructure(
            session::IsolatedProfileEntry{
                .workspace_id = plan.workspace_id,
                .name = plan.name,
                .sort_key =
                    session::NextIsolatedWorkspaceSortKey(main_workspaces),
            },
            std::move(pending));
    switch (started) {
      case session::IsolatedStructureCreation::kStarted:
        break;  // `pending.done` reports.
      case session::IsolatedStructureCreation::kAlreadyExists:
        barrier.Run(SeparatedResult::kExisting);
        break;
      case session::IsolatedStructureCreation::kBeingDeleted:
        barrier.Run(SeparatedResult::kFailed);
        break;
    }
  }
}

}  // namespace ahoi::importer::arc
