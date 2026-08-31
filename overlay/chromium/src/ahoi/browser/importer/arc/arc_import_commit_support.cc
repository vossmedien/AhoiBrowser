// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_commit_support.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_journal.h"
#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"

namespace ahoi::importer::arc {

std::string ArcImportSnapshotToken(const ArcImportSnapshot& snapshot) {
  return base::HexEncodeLower(snapshot.sha256);
}

std::vector<std::string> ArcImportAffectedIds(
    const ArcImportPlan& runtime_plan) {
  std::set<std::string> ids;
  for (const tab_tree::Workspace& workspace : runtime_plan.tree.workspaces) {
    ids.insert(workspace.id.AsLowercaseString());
  }
  for (const tab_tree::TreeNode& node : runtime_plan.tree.nodes) {
    ids.insert(node.id.AsLowercaseString());
  }
  for (const ArcSplitDescriptor& split : runtime_plan.splits) {
    ids.insert(split.folder_node_id.AsLowercaseString());
    for (const base::Uuid& member : split.member_node_ids) {
      ids.insert(member.AsLowercaseString());
    }
  }
  return {ids.begin(), ids.end()};
}

ArcImportStatus ValidateArcImportCommitSource(
    const base::FilePath& profile_path,
    const ArcSource& source,
    const std::string& expected_token) {
  const ArcImportJournalReadResult journal = ReadArcImportJournal(profile_path);
  if (journal.status != ArcImportStatus::kOk) {
    return journal.status;
  }
  if (journal.state == ArcImportJournalState::kPrepared) {
    return ArcImportStatus::kRecoveryRequired;
  }
  if (IsArcApplicationRunning() || AreArcProfileFilesOpen(source)) {
    return ArcImportStatus::kSourceInUse;
  }
  ArcSnapshotResult snapshot = CaptureArcSnapshot(source);
  if (snapshot.status != ArcImportStatus::kOk || !snapshot.snapshot) {
    return snapshot.status;
  }
  return ArcImportSnapshotToken(*snapshot.snapshot) == expected_token
             ? ArcImportStatus::kOk
             : ArcImportStatus::kSourceChanged;
}

bool IsValidArcImportSelection(const ArcImportSelection& selection,
                               const ArcSource& source) {
  if (!selection.import_sidebar || !selection.backup_confirmed ||
      !selection.commit_confirmed ||
      selection.selected_browser_profiles.empty()) {
    return false;
  }
  std::set<std::string> available;
  for (const ArcBrowserProfile& profile : source.browser_profiles) {
    available.insert(profile.directory_name);
  }
  std::set<std::string> selected;
  for (const std::string& profile : selection.selected_browser_profiles) {
    if (!available.contains(profile) || !selected.insert(profile).second) {
      return false;
    }
  }
  return true;
}

ArcSource SelectArcImportBrowserProfiles(const ArcSource& source,
                                         const ArcImportSelection& selection) {
  ArcSource selected = source;
  std::erase_if(selected.browser_profiles,
                [&selection](const ArcBrowserProfile& profile) {
                  return std::ranges::find(selection.selected_browser_profiles,
                                           profile.directory_name) ==
                         selection.selected_browser_profiles.end();
                });
  return selected;
}

ArcImportPlan SelectArcImportCategories(const ArcImportPlan& plan,
                                        const ArcImportSelection& selection) {
  ArcImportPlan selected = plan;
  if (!selection.reconstruct_splits) {
    selected.stats.degraded_split_count += selected.stats.imported_split_count;
    selected.stats.imported_split_count = 0;
    selected.splits.clear();
  }
  return selected;
}

}  // namespace ahoi::importer::arc
