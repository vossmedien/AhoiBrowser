// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_COMMIT_SUPPORT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_COMMIT_SUPPORT_H_

#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

struct ArcImportSelection;

std::string ArcImportSnapshotToken(const ArcImportSnapshot& snapshot);
std::vector<std::string> ArcImportAffectedIds(
    const ArcImportPlan& runtime_plan);
ArcImportStatus ValidateArcImportCommitSource(
    const base::FilePath& profile_path,
    const ArcSource& source,
    const std::string& expected_token);
bool IsValidArcImportSelection(const ArcImportSelection& selection,
                               const ArcSource& source);
ArcSource SelectArcImportBrowserProfiles(const ArcSource& source,
                                         const ArcImportSelection& selection);
ArcImportPlan SelectArcImportCategories(const ArcImportPlan& plan,
                                        const ArcImportSelection& selection);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_COMMIT_SUPPORT_H_
