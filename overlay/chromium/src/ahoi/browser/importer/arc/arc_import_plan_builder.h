// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PLAN_BUILDER_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PLAN_BUILDER_H_

#include <map>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "ahoi/browser/importer/arc/arc_import_types.h"

namespace ahoi::importer::arc::internal {

// Turns a read and graph-validated Arc source into the import plan: one
// Workspace per ordered space (plus, with `options.folders_as_workspaces`, one
// per top-level pinned folder), pinned and unpinned roots, folders, tabs and
// splits with deterministic, layout-separated IDs. `plan` carries the parser's
// source counts in and the complete plan out; on failure only the status is
// meaningful (split from arc_import_parser.cc, source line budget).
ArcImportStatus BuildArcImportPlan(
    const std::map<std::string, SourceSpace>& spaces,
    const std::map<std::string, SourceItem>& items,
    const std::vector<std::string>& ordered_space_ids,
    const ArcImportPlanOptions& options,
    ArcImportPlan* plan);

}  // namespace ahoi::importer::arc::internal

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PLAN_BUILDER_H_
