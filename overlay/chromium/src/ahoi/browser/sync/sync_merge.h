// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_MERGE_H_
#define AHOI_BROWSER_SYNC_SYNC_MERGE_H_

#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

inline constexpr int64_t kMinimumSyncClockPhysicalUs = 11644473600000000;
bool IsCanonicalSyncDeviceId(std::string_view value);
bool IsValidSyncClock(const HlcStamp& stamp);

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

// Local authoring helper: a new current-format record may receive its initial
// full map from its explicit creation clock. Old versions and partial maps are
// rejected. Incoming records must pass HasCompleteFieldVersions beforehand.
bool NormalizeFieldVersions(SyncRecord* record, std::string* error = nullptr);

// Requires the one current model and exactly the complete known field-clock
// set. No legacy normalization is performed at this untrusted boundary.
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

// Shared native/wire content boundary. This does not invent record identity or
// field clocks merely to check local metadata before any journal write.
bool ValidateBookmarkContent(BookmarkKind kind,
                             const std::string& title,
                             const std::string& url,
                             base::Time created_at,
                             std::string* error = nullptr);

// Local pre-authoring shape/graph checks. These deliberately ignore clocks;
// they never authorize transport or a journal write. Wire/store callers must
// use ValidateRecord/ValidateBookmarkGraph instead.
bool ValidateNativeBookmarkShape(const BookmarkRecord& record,
                                 std::string* error = nullptr);
bool ValidateNativeBookmarkGraph(const std::vector<BookmarkRecord>& records,
                                 std::string* error = nullptr);

// Validates all active tree rows as one graph. A provider may deliver parents
// and children in either order; callers should pass the candidate post-merge
// set so this catches self-parenting, cross-workspace links, non-folder
// parents, and cycles before committing a batch.
bool ValidateTreeGraph(const std::vector<TreeNodeRecord>& nodes,
                       std::string* error = nullptr);

// Validates the known bookmark graph without recursion. Missing ancestors may
// arrive on a later provider page and remain detached; callers must not apply
// such a branch to BookmarkModel until its complete live ancestry is known.
bool ValidateBookmarkGraph(const std::vector<BookmarkRecord>& records,
                           std::string* error = nullptr);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_MERGE_H_
