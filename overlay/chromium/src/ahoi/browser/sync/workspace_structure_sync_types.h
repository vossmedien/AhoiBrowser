// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AHOI_BROWSER_SYNC_WORKSPACE_STRUCTURE_SYNC_TYPES_H_
#define AHOI_BROWSER_SYNC_WORKSPACE_STRUCTURE_SYNC_TYPES_H_
#include <map>
#include "ahoi/browser/sync/sync_authorization.h"
#include "ahoi/browser/sync/sync_model.h"
namespace ahoi::sync {
struct WorkspaceStructureProjection {
  std::vector<SplitGroupRecord> split_groups;
  std::vector<TabArchiveEntryRecord> archive_entries;
  // Raw records remain retained when page/workspace references are missing.
  // Native validates actual references/protections before any materialization.
  std::map<base::Uuid, std::string> canonical_payloads;
  HlcStamp observed_clock;
  bool initial_fetch_complete = false;
  SyncAuthorization authorization;
};
struct WorkspaceStructureIntent {
  // Only SplitGroup/TabArchiveEntry are accepted by this seam. Caller supplies
  // the original persisted intent's complete version; retries never restamp it.
  SyncRecord record;
  // Null means exact absence observed; otherwise a byte-exact compare-and-set.
  std::optional<std::string> expected_payload;
  SyncAuthorization authorization;
};
}  // namespace ahoi::sync
#endif
