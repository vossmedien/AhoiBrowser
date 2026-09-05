// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac_internal.h"

namespace ahoi::sync {

void CloudKitSyncProviderMac::Core::DispatchUpload(
    UploadCallback callback,
    uint64_t generation,
    bool success,
    std::vector<std::string> acknowledged,
    std::string error) {
  if (!callback) {
    return;
  }
  owner_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<Core> weak, uint64_t generation,
             UploadCallback callback, bool success,
             std::vector<std::string> acknowledged, std::string error) {
            const auto core = weak.lock();
            bool current = false;
            if (core) {
              base::AutoLock guard(core->lock_);
              current = !core->shutting_down_ &&
                        generation == core->transport_generation_ &&
                        !core->account_transition_pending_ &&
                        !core->zone_recovery_pending_;
            }
            std::move(callback).Run(
                current && success,
                current ? std::move(acknowledged) : std::vector<std::string>(),
                current ? std::move(error) : "cancelled");
          },
          weak_from_this(), generation, std::move(callback), success,
          std::move(acknowledged), std::move(error)));
}

void CloudKitSyncProviderMac::Core::DispatchDownload(DownloadCallback callback,
                                                     uint64_t generation,
                                                     bool success,
                                                     ProviderBatch batch,
                                                     std::string error) {
  if (!callback) {
    return;
  }
  owner_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::weak_ptr<Core> weak, uint64_t generation,
                        DownloadCallback callback, bool success,
                        ProviderBatch batch, std::string error) {
                       const auto core = weak.lock();
                       bool current = false;
                       if (core) {
                         base::AutoLock guard(core->lock_);
                         current = !core->shutting_down_ &&
                                   generation == core->transport_generation_ &&
                                   !core->account_transition_pending_ &&
                                   !core->zone_recovery_pending_;
                       }
                       std::move(callback).Run(
                           current && success,
                           current ? std::move(batch) : ProviderBatch(),
                           current ? std::move(error) : "cancelled");
                     },
                     weak_from_this(), generation, std::move(callback), success,
                     std::move(batch), std::move(error)));
}

void CloudKitSyncProviderMac::Core::RequestOperationCancellation() {
  lock_.AssertAcquired();
  if (!engine_) {
    return;
  }
  operations_cancelling_ = true;
  const uint64_t generation = transport_generation_;
  std::weak_ptr<Core> weak = weak_from_this();
  CKSyncEngine* engine = engine_;
  // Do not call a potentially reentrant framework completion while holding
  // the Core lock. The category and record-provider fences are already active.
  owner_runner_->PostTask(FROM_HERE, base::BindOnce([weak, engine, generation] {
                            [engine cancelOperationsWithCompletionHandler:^{
                              if (const auto core = weak.lock()) {
                                base::AutoLock guard(core->lock_);
                                if (generation == core->transport_generation_) {
                                  core->operations_cancelling_ = false;
                                }
                              }
                            }];
                          }));
}

void CloudKitSyncProviderMac::Core::SetBookmarkSyncEnabled(bool enabled) {
  base::AutoLock guard(lock_);
  if (shutting_down_) {
    return;
  }
  // Account recovery restores transport authority, never category consent.
  enabled = enabled && !account_transition_pending_;
  if (bookmark_sync_enabled_ == enabled &&
      (!enabled || !bookmark_consent_revoked_)) {
    return;
  }
  bookmark_sync_enabled_ = enabled;
  if (enabled) {
    bookmark_consent_revoked_ = false;
  }
  ++transport_generation_;
  DispatchUpload(std::move(upload_callback_), transport_generation_, false, {},
                 "cancelled");
  DispatchDownload(std::move(download_callback_), transport_generation_, false,
                   {}, "cancelled");
  upload_acknowledgements_.clear();
  RequestOperationCancellation();
  if (!enabled) {
    NSMutableArray* blocked = [NSMutableArray array];
    for (auto it = pending_records_.begin(); it != pending_records_.end();) {
      if (!IsBookmarkRecord(it->second)) {
        ++it;
        continue;
      }
      for (CKSyncEnginePendingRecordZoneChange* change in engine_.state
               .pendingRecordZoneChanges) {
        if ([change.recordID isEqual:it->second.recordID]) {
          [blocked addObject:change];
        }
      }
      pending_mutations_.erase(it->first);
      it = pending_records_.erase(it);
    }
    [engine_.state removePendingRecordZoneChanges:blocked];
    std::erase_if(fetched_changes_, [](const auto& entry) {
      return entry.second.entity_type == EntityType::kBookmark;
    });
    // Keep the ciphertext until a new approved delivery is acknowledged.
    materialized_bookmark_keys_.clear();
  } else {
    HydrateDeferredBookmarks();
  }
  PersistInbox();
}

void CloudKitSyncProviderMac::Core::ResetAccountState() {
  lock_.AssertAcquired();
  account_transition_pending_ = true;
  bookmark_sync_enabled_ = false;
  bookmark_consent_revoked_ = true;
  ++transport_generation_;
  DispatchUpload(std::move(upload_callback_), transport_generation_, false, {},
                 "account_unavailable");
  DispatchDownload(std::move(download_callback_), transport_generation_, false,
                   {}, "account_unavailable");
  fetched_changes_.clear();
  opaque_bookmark_records_.clear();
  legacy_bookmark_changes_.clear();
  materialized_bookmark_keys_.clear();
  bookmark_quarantine_ids_.clear();
  last_delivery_mutations_.clear();
  last_delivery_token_.clear();
  pending_records_.clear();
  server_records_.clear();
  pending_mutations_.clear();
  upload_acknowledgements_.clear();
  [engine_.state
      removePendingRecordZoneChanges:engine_.state.pendingRecordZoneChanges];
  RequestOperationCancellation();
  (void)base::DeleteFile(state_path_);
  PersistInbox();
}

void CloudKitSyncProviderMac::Core::ReceiveFetchedRecord(CKRecord* record) {
  lock_.AssertAcquired();
  if (IsBookmarkRecord(record)) {
    const std::string key = ToString(record.recordID.recordName);
    const auto previous = materialized_bookmark_keys_.find(key);
    if (previous != materialized_bookmark_keys_.end()) {
      fetched_changes_.erase(previous->second);
      materialized_bookmark_keys_.erase(previous);
    }
    opaque_bookmark_records_[key] = [record copy];
    bookmark_quarantine_ids_.erase(key);
    legacy_bookmark_changes_.erase(key);
    if (BookmarkAllowed()) {
      MaterializeBookmarkRecord(key, record);
    }
    return;
  }
  auto change = Decode(record);
  if (!change) {
    change = MakeCloudKitQuarantineMarker(
        EntityTypeForDataClass(record[@"dataClass"])
            .value_or(EntityType::kDevice));
  }
  fetched_changes_[change->entity_id.AsLowercaseString()] = std::move(*change);
}

void CloudKitSyncProviderMac::Core::MaterializeBookmarkRecord(
    const std::string& key,
    CKRecord* record) {
  lock_.AssertAcquired();
  if (!BookmarkAllowed() || materialized_bookmark_keys_.contains(key)) {
    return;
  }
  auto change = Decode(record);
  if (!change) {
    change = MakeCloudKitQuarantineMarker(EntityType::kBookmark);
    const auto stored =
        bookmark_quarantine_ids_.try_emplace(key, change->entity_id).first;
    change->entity_id = stored->second;
    change->mutation_id = "cloud-invalid:" + stored->second.AsLowercaseString();
  }
  const std::string materialized_key = change->entity_id.AsLowercaseString();
  materialized_bookmark_keys_[key] = materialized_key;
  fetched_changes_[materialized_key] = std::move(*change);
}

void CloudKitSyncProviderMac::Core::HydrateDeferredBookmarks() {
  lock_.AssertAcquired();
  if (!BookmarkAllowed()) {
    return;
  }
  for (const auto& [key, record] : opaque_bookmark_records_) {
    MaterializeBookmarkRecord(key, record);
  }
  HydrateLegacyBookmarks();
}

CKRecord* CloudKitSyncProviderMac::Core::PendingRecordForGeneration(
    const std::string& key,
    uint64_t generation) {
  base::AutoLock guard(lock_);
  if (shutting_down_ || account_transition_pending_ || zone_recovery_pending_ ||
      operations_cancelling_ || generation != transport_generation_) {
    return nil;
  }
  const auto record = pending_records_.find(key);
  return record == pending_records_.end() ||
                 (IsBookmarkRecord(record->second) && !BookmarkAllowed())
             ? nil
             : record->second;
}

void CloudKitSyncProviderMac::Core::Upload(std::vector<SyncChange> changes,
                                           UploadCallback callback) {
  CKSyncEngine* engine;
  AhoiCloudKitSyncDelegate* delegate;
  uint64_t generation;
  CKSyncEngineSendChangesOptions* options;
  {
    base::AutoLock guard(lock_);
    generation = transport_generation_;
    if (shutting_down_ || !engine_ || account_transition_pending_ ||
        zone_recovery_pending_ || operations_cancelling_) {
      DispatchUpload(std::move(callback), generation, false, {},
                     operations_cancelling_ ? "temporarily_unavailable"
                                            : "account_unavailable");
      return;
    }
    NSMutableArray* pending = [NSMutableArray array];
    std::map<std::string, __strong CKRecord*> records;
    std::map<std::string, std::string> mutations;
    for (const auto& change : changes) {
      if (change.entity_type == EntityType::kBookmark && !BookmarkAllowed()) {
        DispatchUpload(std::move(callback), generation, false, {}, "cancelled");
        return;
      }
      SyncRecord decoded;
      if (!ValidateChangeEnvelope(change, &decoded)) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "provider_error");
        return;
      }
      auto sealed = cryptor_->Seal(change.payload);
      if (!sealed) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "account_unavailable");
        return;
      }
      const std::string key = change.entity_id.AsLowercaseString();
      CKRecordID* record_id =
          [[CKRecordID alloc] initWithRecordName:ToNSString(key)
                                          zoneID:zone_id_];
      const auto server = server_records_.find(key);
      CKRecord* record = EncodeCloudKitSyncRecord(
          change, *sealed, record_id,
          server == server_records_.end() ? nil : server->second);
      if (!record) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "provider_error");
        return;
      }
      records[key] = record;
      mutations[key] = change.mutation_id;
      [pending
          addObject:
              [[CKSyncEnginePendingRecordZoneChange alloc]
                  initWithRecordID:record_id
                              type:
                                  CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]];
    }
    for (const auto& [key, record] : records) {
      pending_records_[key] = record;
    }
    for (const auto& [key, mutation] : mutations) {
      pending_mutations_[key] = mutation;
    }
    upload_callback_ = std::move(callback);
    upload_acknowledgements_.clear();
    upload_error_.clear();
    [engine_.state addPendingRecordZoneChanges:pending];
    auto* scope = [[CKSyncEngineSendChangesScope alloc]
        initWithZoneIDs:[NSSet setWithObject:zone_id_]];
    options = [[CKSyncEngineSendChangesOptions alloc] initWithScope:scope];
    engine = engine_;
    delegate = delegate_core_;
  }
  __weak AhoiCloudKitSyncDelegate* weak_delegate = delegate;
  [engine sendChangesWithOptions:options
               completionHandler:^(NSError* error) {
                 [weak_delegate completeUpload:error generation:generation];
               }];
}

void CloudKitSyncProviderMac::Core::Download(std::string change_token,
                                             DownloadCallback callback) {
  CKSyncEngine* engine;
  AhoiCloudKitSyncDelegate* delegate;
  CKSyncEngineFetchChangesOptions* options;
  uint64_t generation;
  {
    base::AutoLock guard(lock_);
    generation = transport_generation_;
    if (shutting_down_ || !engine_ || account_transition_pending_ ||
        zone_recovery_pending_ || operations_cancelling_) {
      DispatchDownload(std::move(callback), generation, false, {},
                       operations_cancelling_ ? "temporarily_unavailable"
                                              : "account_unavailable");
      return;
    }
    AcknowledgeLastDelivery(change_token);
    HydrateDeferredBookmarks();
    download_base_token_ = std::move(change_token);
    download_callback_ = std::move(callback);
    download_error_.clear();
    auto* scope = [[CKSyncEngineFetchChangesScope alloc]
        initWithZoneIDs:[NSSet setWithObject:zone_id_]];
    options = [[CKSyncEngineFetchChangesOptions alloc] initWithScope:scope];
    engine = engine_;
    delegate = delegate_core_;
  }
  __weak AhoiCloudKitSyncDelegate* weak_delegate = delegate;
  [engine fetchChangesWithOptions:options
                completionHandler:^(NSError* error) {
                  [weak_delegate completeDownload:error generation:generation];
                }];
}

void CloudKitSyncProviderMac::Core::CompleteUpload(NSError* error,
                                                   uint64_t generation) {
  base::AutoLock guard(lock_);
  if (shutting_down_ || generation != transport_generation_ ||
      !upload_callback_) {
    return;
  }
  const std::string safe_error =
      !upload_error_.empty() ? upload_error_ : SafeCloudKitError(error);
  DispatchUpload(
      std::move(upload_callback_), generation, safe_error.empty(),
      {upload_acknowledgements_.begin(), upload_acknowledgements_.end()},
      safe_error);
}

void CloudKitSyncProviderMac::Core::CompleteDownload(NSError* error,
                                                     uint64_t generation) {
  base::AutoLock guard(lock_);
  if (shutting_down_ || generation != transport_generation_ ||
      !download_callback_) {
    return;
  }
  std::string safe_error =
      !download_error_.empty() ? download_error_ : SafeCloudKitError(error);
  ProviderBatch batch;
  if (safe_error.empty()) {
    for (const auto& [id, change] : fetched_changes_) {
      if (change.entity_type == EntityType::kBookmark && !BookmarkAllowed()) {
        continue;
      }
      batch.changes.push_back(change);
      last_delivery_mutations_[id] = change.mutation_id;
    }
    if (!batch.changes.empty()) {
      last_delivery_token_ = base::StringPrintf(
          "cksync-%llu",
          static_cast<unsigned long long>(++download_generation_));
      batch.next_change_token = last_delivery_token_;
    } else {
      batch.next_change_token = download_base_token_;
    }
    if (!PersistInbox()) {
      safe_error = "provider_error";
      batch = {};
    }
  }
  DispatchDownload(std::move(download_callback_), generation,
                   safe_error.empty(), std::move(batch), safe_error);
}

void CloudKitSyncProviderMac::Core::LoadInboxForTesting() {
  base::AutoLock guard(lock_);
  LoadInbox();
  HydrateDeferredBookmarks();
}

void CloudKitSyncProviderMac::Core::ReceiveRecordForTesting(CKRecord* record) {
  base::AutoLock guard(lock_);
  if (shutting_down_ || account_transition_pending_) {
    return;
  }
  ReceiveFetchedRecord(record);
  PersistInbox();
}

void CloudKitSyncProviderMac::Core::ReadCachedChangesForTesting(
    std::string token,
    DownloadCallback callback) {
  uint64_t generation;
  {
    base::AutoLock guard(lock_);
    AcknowledgeLastDelivery(token);
    HydrateDeferredBookmarks();
    generation = transport_generation_;
    download_base_token_ = std::move(token);
    download_callback_ = std::move(callback);
  }
  CompleteDownload(nil, generation);
}

void CloudKitSyncProviderMac::Core::AccountChangedForTesting() {
  base::AutoLock guard(lock_);
  ResetAccountState();
}

base::RepeatingCallback<bool()>
CloudKitSyncProviderMac::Core::MakeDelayedRecordDeliveryForTesting(
    CKRecord* record) {
  base::AutoLock guard(lock_);
  const std::string key = ToString(record.recordID.recordName);
  pending_records_[key] = record;
  return base::BindRepeating(
      [](std::weak_ptr<Core> weak, std::string key, uint64_t generation) {
        const auto core = weak.lock();
        return core && core->PendingRecordForGeneration(key, generation) != nil;
      },
      weak_from_this(), key, transport_generation_);
}

std::unique_ptr<CloudKitSyncProviderMac>
CloudKitSyncProviderMac::CreateForConsentTesting(
    const base::FilePath& path,
    std::unique_ptr<SyncPayloadCryptor> cryptor,
    bool enabled) {
  auto core = std::make_shared<Core>(CloudKitSyncConfigurationMac(), path,
                                     std::move(cryptor), enabled);
  core->LoadInboxForTesting();
  return std::unique_ptr<CloudKitSyncProviderMac>(
      new CloudKitSyncProviderMac(std::move(core)));
}

void CloudKitSyncProviderMac::ReceiveRecordForTesting(CKRecord* record) {
  core_->ReceiveRecordForTesting(record);
}
void CloudKitSyncProviderMac::ReadCachedChangesForTesting(
    std::string token,
    DownloadCallback callback) {
  core_->ReadCachedChangesForTesting(std::move(token), std::move(callback));
}
void CloudKitSyncProviderMac::AccountChangedForTesting() {
  core_->AccountChangedForTesting();
}

base::RepeatingCallback<bool()>
CloudKitSyncProviderMac::MakeDelayedRecordDeliveryForTesting(CKRecord* record) {
  return core_->MakeDelayedRecordDeliveryForTesting(record);
}

}  // namespace ahoi::sync
#pragma clang diagnostic pop
