// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_MERGE_H_
#define AHOI_BROWSER_SYNC_SYNC_MERGE_H_

#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

enum class MergeDecision {
  kKeepExisting,
  kAcceptIncoming,
  kMergeFields,
  kDuplicate,
  kInvalid,
};

// Last-writer-wins is applied to one complete record, with HLC/device ordering
// providing deterministic convergence. The operation is idempotent for an
// identical version/payload and rejects malformed equal-version conflicts.
MergeDecision DecideMerge(const SyncVersion& existing_version,
                          const std::string& existing_payload,
                          const SyncVersion& incoming_version,
                          const std::string& incoming_payload);

// Populates missing wire-v2 field clocks from the record clock. Wire-v1 data
// is upgraded in memory this way; unknown field keys fail closed.
bool NormalizeFieldVersions(SyncRecord* record, std::string* error = nullptr);

// Returns true for legacy v1 records and for v2 records carrying exactly the
// complete known field-clock set. Used at the untrusted wire boundary.
bool HasCompleteFieldVersions(const SyncRecord& record);

// Prepares a local write without turning an update to one scalar into a write
// to every scalar. Unchanged fields retain their prior clocks and changed
// fields receive the caller-provided record clock.
bool StampLocalMutation(const SyncRecord* existing,
                        SyncRecord* local,
                        std::string* error = nullptr);

// Deterministically merges independently versioned fields. Immutable identity
// fields and equal-clock/different-value inputs are rejected for quarantine.
// kMergeFields means neither complete input contains the converged union and
// the caller must persist and enqueue `merged` for convergence.
MergeDecision MergeRecordFields(const SyncRecord& existing,
                                const SyncRecord& incoming,
                                SyncRecord* merged,
                                std::string* error = nullptr);

// Validates an individual record before it reaches SQLite. This is deliberately
// transport-independent and can be reused by a future iOS decoder.
bool ValidateRecord(const SyncRecord& record, std::string* error = nullptr);

// Validates all active tree rows as one graph. A provider may deliver parents
// and children in either order; callers should pass the candidate post-merge
// set so this catches self-parenting, cross-workspace links, non-folder
// parents, and cycles before committing a batch.
bool ValidateTreeGraph(const std::vector<TreeNodeRecord>& nodes,
                       std::string* error = nullptr);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_MERGE_H_
