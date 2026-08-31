// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "base/memory/weak_ptr.h"

namespace tabs {
class TabInterface;
}

namespace ahoi::importer::arc {

// Shared only by ArcImportService implementation translation units. Keeping
// transaction ownership here lets recovery, native-session receipt handling,
// and final journal publication remain separate, sub-800-line components.
struct ArcImportService::CommitContext {
  ArcImportCommitCallback callback;
  ArcImportCommitResult result;
  base::WeakPtr<BrowserWindowInterface> browser;
  ArcSource selected_source;
  ArcImportPlan selected_plan;
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
  bool same_key_replay = false;
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_INTERNAL_H_
