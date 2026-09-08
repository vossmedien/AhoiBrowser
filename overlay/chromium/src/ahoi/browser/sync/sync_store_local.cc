// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/sync_unified_validation.h"
#include "sql/transaction.h"

namespace ahoi::sync {

SyncStore::Result SyncStore::PutLocalRecord(const SyncRecord& record,
                                            std::string mutation_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  const Result result =
      PutLocalRecordInTransaction(record, std::move(mutation_id));
  if (result != Result::kOk) {
    // Idempotent replays retain their result even if a related Page/Device
    // has changed since that original mutation. No new write is being admitted.
    return result;
  }
  if (!ValidateLocalRecordReferences(record)) {
    return Result::kInvalidArgument;
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  if (result == Result::kOk) {
    NotifyChanged();
  }
  return result;
}

SyncStore::Result SyncStore::PutLocalBatch(
    const std::vector<SyncRecord>& records,
    const SyncAuthorization& authorization) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsReady()) {
    return Result::kNotInitialized;
  }
  if (!authorization || !authorization.Run()) {
    return Result::kNotAuthorized;
  }
  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return Result::kDatabaseError;
  }
  bool changed = false;
  std::set<std::pair<EntityType, base::Uuid>> identities;
  for (const auto& record : records) {
    if (!identities.emplace(GetEntityType(record), GetEntityId(record))
             .second) {
      return Result::kInvalidArgument;
    }
    const Result result = PutLocalRecordInTransaction(record, {});
    if (result != Result::kOk && result != Result::kAlreadyApplied) {
      return result;
    }
    changed |= result == Result::kOk;
  }
  // All rows exist within this transaction, so local graph references can be
  // checked independent of the caller's ordering. Remote pages use their
  // separate durable/deferred projection boundary, not this local batch path.
  for (const auto& record : records) {
    if (!ValidateLocalRecordReferences(record)) {
      return Result::kInvalidArgument;
    }
  }
  if (!ValidateLocalBatchGraphs(records)) {
    return Result::kInvalidArgument;
  }
  if (!authorization.Run()) {
    return Result::kNotAuthorized;
  }
  if (!transaction.Commit()) {
    return Result::kDatabaseError;
  }
  if (changed) {
    NotifyChanged();
  }
  return Result::kOk;
}

bool SyncStore::ValidateLocalRecordReferences(const SyncRecord& record) const {
  if (const auto* tab = std::get_if<RemoteTabRecord>(&record)) {
    if (tab->tombstone) {
      return true;
    }
    StoredRecord page;
    return tab->tree_node_id &&
           ReadStoredRecord(EntityType::kTreeNode, *tab->tree_node_id, &page) ==
               Result::kOk &&
           SharedPresenceMatchesPage(*tab,
                                     std::get<TreeNodeRecord>(page.record));
  }
  if (const auto* capability = std::get_if<DeviceCapabilityRecord>(&record)) {
    StoredRecord stored;
    if (ReadStoredRecord(EntityType::kDevice, capability->device_id, &stored) !=
        Result::kOk) {
      return false;
    }
    const auto& device = std::get<DeviceRecord>(stored.record);
    return capability->tombstone || (!device.tombstone && !device.retired);
  }
  return true;
}

bool SyncStore::ValidateLocalBatchGraphs(
    const std::vector<SyncRecord>& records) const {
  if (std::ranges::any_of(records, [](const auto& record) {
        return GetEntityType(record) == EntityType::kTreeNode;
      })) {
    std::vector<SyncRecord> stored;
    if (GetRecords(EntityType::kTreeNode, &stored) != Result::kOk) {
      return false;
    }
    std::vector<TreeNodeRecord> graph;
    for (const auto& record : stored) {
      graph.push_back(std::get<TreeNodeRecord>(record));
    }
    if (!ValidateTreeGraph(graph)) {
      return false;
    }
  }
  if (std::ranges::any_of(records, [](const auto& record) {
        return GetEntityType(record) == EntityType::kBookmark;
      })) {
    std::vector<SyncRecord> stored;
    if (GetRecords(EntityType::kBookmark, &stored) != Result::kOk) {
      return false;
    }
    std::vector<BookmarkRecord> graph;
    for (const auto& record : stored) {
      graph.push_back(std::get<BookmarkRecord>(record));
    }
    if (!ValidateBookmarkGraph(graph)) {
      return false;
    }
  }
  return true;
}

}  // namespace ahoi::sync
