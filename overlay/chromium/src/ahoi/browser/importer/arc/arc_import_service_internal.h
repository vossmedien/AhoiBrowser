// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"

namespace tabs {
class TabInterface;
}

namespace ahoi::importer::arc {

struct ArcImportService::DiscoveryResult {
  ArcImportStatus status = ArcImportStatus::kNotFound;
  std::optional<ArcImportPlan> plan;
  std::string snapshot_token;
  std::optional<ArcImportCommittedState> committed;
  std::optional<ArcImportPreparedState> prepared;
  std::optional<ArcSource> source;
  bool arc_is_running = false;
  bool history_recovery_started = false;
  bool history_available = false;
  size_t history_recovery_cursor = 0;
};

// Shared only by ArcImportService implementation translation units. Keeping
// transaction ownership here lets recovery, native-session receipt handling,
// and final journal publication remain separate, sub-800-line components.
struct ArcImportService::CommitContext {
  ArcImportCommitCallback callback;
  ArcImportCommitResult result;
  base::WeakPtr<BrowserWindowInterface> browser;
  ArcSource selected_source;
  bool import_sidebar = true;
  bool import_history = false;
  std::vector<std::string> separated_arc_profiles;
  bool main_history_finished = false;
  size_t history_profile_cursor = 0;
  ArcImportPlan runtime_plan;
  std::optional<tab_tree::TabTreeSnapshot> merged_tree;
  tab_tree::TabTreeSnapshot previous_tree;
  std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs;
  std::string snapshot_hash;
  std::string selection_fingerprint;
  std::string idempotency_key;
  ArcImportPreparedState prepared;
  std::optional<ArcImportCommittedState> next_committed;
  bool tree_changed = false;
  bool runtime_started = false;
  base::ScopedClosureRunner resume_automatic_metadata;
  // WS-ISO-10: created only after this transaction succeeded.
  std::vector<ArcSeparatedWorkspacePlan> separated;
};

struct ArcImportService::ManualRecoveryContext {
  ArcImportPreviewCallback callback;
  ArcImportPreparedState prepared;
  tab_tree::TabTreeSnapshot start_tree;
  tab_tree::TabTreeSnapshot previous_tree;
  tab_tree::TabTreeSnapshot recovery_tree;
  std::vector<base::Uuid> removed_workspaces;
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_
