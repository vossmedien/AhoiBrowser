// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SEPARATED_WORKSPACES_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SEPARATED_WORKSPACES_H_

#include <cstddef>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_profile_mapping.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "base/functional/callback.h"

namespace ahoi::importer::arc {

struct ArcSeparatedImportOutcome {
  // New fully separated Workspaces whose Profile imported the structure.
  size_t created = 0;
  // Arc profiles whose separated Workspace an earlier import created. Their
  // tree is left as it is: a repeated import neither duplicates the Profile
  // nor merges into it (a separated Profile's tree is not reachable from the
  // importing Profile's transaction).
  size_t existing = 0;
  // Creation or import failed (that Profile deletes itself), or the earlier
  // Workspace is still being deleted.
  size_t failed = 0;

  bool operator==(const ArcSeparatedImportOutcome&) const = default;
};

// ADR 0011 WS-ISO-10: creates one fully separated Workspace per plan through
// session::CreateIsolatedWorkspaceWithStructure(), ordered after
// `main_workspaces` and every registered separated Workspace. This runs after
// the main Profile's Arc transaction and is not part of its journal, backup
// or rollback: every separated Workspace is its own Profile, created or
// deleted as a whole. `done` runs once every plan has an outcome.
void CreateArcSeparatedWorkspaces(
    std::vector<ArcSeparatedWorkspacePlan> plans,
    std::vector<session::DirectoryWorkspace> main_workspaces,
    base::OnceCallback<void(ArcSeparatedImportOutcome)> done);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SEPARATED_WORKSPACES_H_
