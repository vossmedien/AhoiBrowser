// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/functional/bind.h"
namespace ahoi::sync {
std::optional<WorkspaceStructureProjection>
ProfileSyncBackend::ReadWorkspaceStructure() {
  auto authority = CaptureBrowserSettingsAuthorization();
  if (!authority || !authority.Run())
    return std::nullopt;
  WorkspaceStructureProjection p;
  for (auto type : {EntityType::kSplitGroup, EntityType::kTabArchiveEntry}) {
    std::vector<SyncRecord> records;
    if (store_->GetRecords(type, &records) != SyncStore::Result::kOk)
      return std::nullopt;
    for (const auto& r : records) {
      std::string payload;
      if (!SerializeRecord(r, &payload))
        return std::nullopt;
      p.canonical_payloads.emplace(GetEntityId(r), std::move(payload));
      if (const auto* split = std::get_if<SplitGroupRecord>(&r))
        p.split_groups.push_back(*split);
      else
        p.archive_entries.push_back(std::get<TabArchiveEntryRecord>(r));
    }
  }
  p.observed_clock = clock_.last();
  p.initial_fetch_complete = store_->HasCompletedInitialFetch();
  p.authorization = std::move(authority);
  return p;
}
std::optional<SyncStateSnapshot>
ProfileSyncBackend::PublishWorkspaceStructureIntent(
    WorkspaceStructureIntent intent) {
  auto current = CaptureBrowserSettingsAuthorization();
  const auto type = GetEntityType(intent.record);
  if (!intent.authorization || !intent.authorization.Run() || !current ||
      !current.Run() || !store_->HasCompletedInitialFetch() ||
      (type != EntityType::kSplitGroup &&
       type != EntityType::kTabArchiveEntry) ||
      !HasCompleteFieldVersions(intent.record) ||
      !ValidateRecord(intent.record) ||
      GetVersion(intent.record).stamp.device_tiebreak !=
          device_id_.AsLowercaseString())
    return std::nullopt;
  SyncRecord old;
  const auto found = store_->GetRecord(type, GetEntityId(intent.record), &old);
  std::string old_payload, new_payload;
  if (!SerializeRecord(intent.record, &new_payload))
    return std::nullopt;
  if (found == SyncStore::Result::kOk) {
    if (!SerializeRecord(old, &old_payload))
      return std::nullopt;
    if (old_payload == new_payload)
      return CurrentState();  // Idempotent ACK of the exact original intent.
    if (!intent.expected_payload || *intent.expected_payload != old_payload ||
        GetVersion(intent.record) <= GetVersion(old) ||
        (type == EntityType::kTabArchiveEntry && IsTombstone(old) &&
         !IsTombstone(intent.record)))
      return std::nullopt;
  } else if (found != SyncStore::Result::kNotFound || intent.expected_payload)
    return std::nullopt;
  auto valid = base::BindRepeating(
      [](SyncAuthorization original, SyncAuthorization current) {
        return original.Run() && current.Run();
      },
      intent.authorization, std::move(current));
  const auto result = store_->PutLocalBatch({intent.record}, valid);
  if (result != SyncStore::Result::kOk &&
      result != SyncStore::Result::kAlreadyApplied)
    return std::nullopt;
  clock_.Restore(GetVersion(intent.record).stamp);
  return CurrentState();
}
}  // namespace ahoi::sync
