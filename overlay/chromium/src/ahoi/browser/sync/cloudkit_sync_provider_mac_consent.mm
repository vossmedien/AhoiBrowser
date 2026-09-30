// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/browser_settings_sync_types.h"
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
  // Saved records have already left the pending map. Keep the original setting
  // leases through the final asynchronous delivery of their acknowledgements.
  const auto setting_authorizations = upload_setting_authorizations_;
  owner_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<Core> weak, uint64_t generation,
             UploadCallback callback, bool success,
             std::vector<std::string> acknowledged, std::string error,
             std::map<std::string, SyncAuthorization> setting_authorizations) {
            const auto core = weak.lock();
            bool current = false;
            if (core) {
              base::AutoLock guard(core->lock_);
              current = core->TransportAllowed() && !core->shutting_down_ &&
                        generation == core->transport_generation_ &&
                        !core->account_transition_pending_ &&
                        !core->zone_recovery_pending_;
              for (const auto& [id, authorization] : setting_authorizations) {
                current = current && authorization && authorization.Run();
              }
            }
            std::move(callback).Run(
                current && success,
                current ? std::move(acknowledged) : std::vector<std::string>(),
                current ? std::move(error) : "cancelled");
          },
          weak_from_this(), generation, std::move(callback), success,
          std::move(acknowledged), std::move(error), setting_authorizations));
}

void CloudKitSyncProviderMac::Core::DispatchDownload(DownloadCallback callback,
                                                     uint64_t generation,
                                                     bool success,
                                                     ProviderBatch batch,
                                                     std::string error) {
  if (!callback) {
    return;
  }
  const auto original_read_authorization = download_authorization_;
  owner_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<Core> weak, uint64_t generation,
             DownloadCallback callback, bool success, ProviderBatch batch,
             std::string error, SyncAuthorization original_read_authorization) {
            const auto core = weak.lock();
            bool current = false;
            if (core) {
              base::AutoLock guard(core->lock_);
              current = core->TransportAllowed() && !core->shutting_down_ &&
                        generation == core->transport_generation_ &&
                        !core->account_transition_pending_ &&
                        !core->zone_recovery_pending_;
            }
            if (success && (!original_read_authorization ||
                            !original_read_authorization.Run())) {
              current = false;
            }
            std::move(callback).Run(
                current && success,
                current ? std::move(batch) : ProviderBatch(),
                current ? std::move(error) : "cancelled");
          },
          weak_from_this(), generation, std::move(callback), success,
          std::move(batch), std::move(error), original_read_authorization));
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
  owner_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<Core> weak_core, CKSyncEngine* sync_engine,
             uint64_t expected_generation) {
            [sync_engine cancelOperationsWithCompletionHandler:^{
              if (const auto core = weak_core.lock()) {
                base::AutoLock guard(core->lock_);
                if (expected_generation == core->transport_generation_) {
                  core->operations_cancelling_ = false;
                }
              }
            }];
          },
          std::move(weak), engine, generation));
}

BookmarkSyncAuthorization
CloudKitSyncProviderMac::Core::GetBookmarkSyncAuthorization() {
  base::AutoLock guard(lock_);
  if (shutting_down_ || !BookmarkAllowed()) {
    return {};
  }
  return base::BindRepeating(
      [](std::weak_ptr<Core> weak, uint64_t generation) {
        const auto core = weak.lock();
        if (!core) {
          return false;
        }
        base::AutoLock guard(core->lock_);
        return !core->shutting_down_ &&
               generation == core->transport_generation_ &&
               core->BookmarkAllowed();
      },
      weak_from_this(), transport_generation_);
}

SyncAuthorization CloudKitSyncProviderMac::Core::GetTransportAuthorization() {
  base::AutoLock guard(lock_);
  if (!TransportAllowed() || shutting_down_ || !engine_ || !cryptor_ ||
      persisted_state_invalid_ || account_transition_pending_ ||
      zone_recovery_pending_) {
    return {};
  }
  return base::BindRepeating(
      [](std::weak_ptr<Core> weak, uint64_t generation) {
        const auto core = weak.lock();
        if (!core) {
          return false;
        }
        base::AutoLock guard(core->lock_);
        return core->TransportAllowed() && !core->shutting_down_ &&
               core->engine_ && core->cryptor_ &&
               !core->persisted_state_invalid_ &&
               !core->account_transition_pending_ &&
               !core->zone_recovery_pending_ &&
               generation == core->transport_generation_;
      },
      weak_from_this(), transport_generation_);
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

void CloudKitSyncProviderMac::Core::HandleAccountChange(
    CKSyncEngineAccountChangeType type,
    CKRecordID* previous_user,
    CKRecordID* current_user) {
  lock_.AssertAcquired();
  const bool verified_same_sign_in =
      type == CKSyncEngineAccountChangeTypeSignIn && !previous_user &&
      current_user && !account_transition_pending_ &&
      !persisted_state_invalid_ && key_setup_issue_.empty() &&
      profile_authorization_ && profile_authorization_.Run() &&
      !configuration_.verified_account_record_name.empty() &&
      configuration_.verified_key_sha256.size() == 64 &&
      configuration_.verified_key_authorization &&
      configuration_.verified_key_authorization.Run() &&
      [current_user.recordName
          isEqualToString:ToNSString(
                              configuration_.verified_account_record_name)];
  if (!verified_same_sign_in) {
    // Sign-out, switch, unknown/unverified identities, revoked original key
    // authority and an already-persisted transition remain fail-closed.
    ResetAccountState();
    return;
  }
  // A fresh/rebuilt CKSyncEngine can report sign-in for the already verified
  // account. It resets its pending changes on account events; retain original
  // record bodies, mutation IDs and consent leases and restore only that queue.
  // No recovery flag is cleared and no authorization generation is renewed.
  if (engine_ && zone_id_ && !zone_recovery_pending_) {
    CKRecordZone* zone = [[CKRecordZone alloc] initWithZoneID:zone_id_];
    [engine_.state
        addPendingDatabaseChanges:@[ [[CKSyncEnginePendingZoneSave alloc]
                                      initWithZone:zone] ]];
    NSMutableArray* pending = [NSMutableArray array];
    for (const auto& [key, record] : pending_records_) {
      if ((IsBookmarkRecord(record) && !BookmarkAllowed()) ||
          !PendingSettingAllowed(record)) {
        continue;
      }
      [pending
          addObject:
              [[CKSyncEnginePendingRecordZoneChange alloc]
                  initWithRecordID:record.recordID
                              type:
                                  CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]];
    }
    [engine_.state addPendingRecordZoneChanges:pending];
  }
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
  materialized_bookmark_keys_.clear();
  bookmark_quarantine_ids_.clear();
  last_delivery_mutations_.clear();
  last_delivery_token_.clear();
  pending_records_.clear();
  pending_setting_authorizations_.clear();
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
  if (!TransportAllowed() ||
      (zone_id_ && ![record.recordID.zoneID isEqual:zone_id_])) {
    return;
  }
  if (IsCloudKitKeyBootstrapRecord(record)) {
    if (!MatchesCloudKitKeyBootstrapRecord(
            record, zone_id_, configuration_.key_version,
            configuration_.verified_key_sha256)) {
      key_setup_issue_ = "key_setup_claim_changed";
      ++transport_generation_;
      RequestOperationCancellation();
    }
    return;  // Authenticated control metadata is never a domain/quarantine row.
  }
  if (IsBookmarkRecord(record)) {
    const std::string key = ToString(record.recordID.recordName);
    const auto previous = materialized_bookmark_keys_.find(key);
    if (previous != materialized_bookmark_keys_.end()) {
      fetched_changes_.erase(previous->second);
      materialized_bookmark_keys_.erase(previous);
    }
    opaque_bookmark_records_[key] = [record copy];
    bookmark_quarantine_ids_.erase(key);
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
}

CKRecord* CloudKitSyncProviderMac::Core::PendingRecordForGeneration(
    const std::string& key,
    uint64_t generation) {
  base::AutoLock guard(lock_);
  if (!TransportAllowed() || shutting_down_ || account_transition_pending_ ||
      zone_recovery_pending_ || operations_cancelling_ ||
      generation != transport_generation_) {
    return nil;
  }
  const auto record = pending_records_.find(key);
  if (record == pending_records_.end() ||
      (IsBookmarkRecord(record->second) && !BookmarkAllowed()) ||
      !PendingSettingAllowed(record->second) || !UploadKeyAuthorized(key))
    return nil;
  // Recheck at the SDK's actual record request, not only while preparing the
  // batch. A fetched/conflict record may have advanced the known server state.
  const auto server = server_records_.find(key);
  if (server != server_records_.end()) {
    const auto local = Decode(record->second);
    const auto remote = Decode(server->second);
    SyncRecord local_record, remote_record;
    if (!local || !remote || !ValidateChangeEnvelope(*local, &local_record) ||
        !ValidateChangeEnvelope(*remote, &remote_record) ||
        !DomainCovers(local_record, remote_record)) {
      if (remote && StageUploadMergeInput(key, *remote, server->second)) {
        NoteMergeRequired();
      } else {
        ++upload_unresolved_count_;
        upload_error_ = "provider_error";
        upload_failure_stage_ = "domain_merge_required";
      }
      return nil;
    }
  }
  return record->second;
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
    if (!TransportAllowed() || shutting_down_ || !engine_ ||
        account_transition_pending_ || zone_recovery_pending_ ||
        operations_cancelling_) {
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
  download_authorization_.Reset();
  download_setting_authorizations_.clear();
  if (safe_error.empty()) {
    last_delivery_mutations_.clear();
    for (const auto& [id, change] : fetched_changes_) {
      if (change.entity_type == EntityType::kBookmark && !BookmarkAllowed()) {
        continue;
      }
      if (change.entity_type == EntityType::kPermittedSetting) {
        auto scope = GetSettingAuthorization(change.entity_id);
        if (!scope || !scope.Run())
          continue;
        download_setting_authorizations_.push_back(std::move(scope));
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
      // No selected record means no delivery can be acknowledged. Blocked
      // settings remain in fetched_changes_, with their bytes untouched.
      last_delivery_token_.clear();
      batch.next_change_token = download_base_token_;
    }
    if (!PersistInbox()) {
      safe_error = "provider_error";
      batch = {};
    }
    download_authorization_token_ = batch.next_change_token;
    download_authorization_ =
        BindReadAuthorization(generation, download_setting_authorizations_);
  }
  DispatchDownload(std::move(download_callback_), generation,
                   safe_error.empty(), std::move(batch), safe_error);
}

void CloudKitSyncProviderMac::Core::SetIncomingCallback(
    IncomingCallback callback) {
  base::AutoLock guard(lock_);
  incoming_callback_ = std::move(callback);
  ++incoming_notification_id_;
  incoming_notification_pending_ = false;
  ScheduleIncomingNotification();
}

void CloudKitSyncProviderMac::Core::ScheduleIncomingNotification() {
  lock_.AssertAcquired();
  if (!incoming_callback_ || incoming_notification_pending_ ||
      !TransportAllowed() || inbox_persistence_failed_ ||
      fetched_changes_.empty()) {
    return;
  }
  bool deliverable = false;
  std::vector<SyncAuthorization> settings;
  for (const auto& [id, change] : fetched_changes_) {
    if (change.entity_type == EntityType::kPermittedSetting) {
      auto scope = GetSettingAuthorization(change.entity_id);
      if (scope && scope.Run()) {
        settings.push_back(std::move(scope));
        deliverable = true;
      }
      continue;
    }
    deliverable |=
        change.entity_type != EntityType::kBookmark || BookmarkAllowed();
  }
  if (!deliverable) {
    return;
  }
  const uint64_t generation = transport_generation_;
  const uint64_t notification = ++incoming_notification_id_;
  incoming_notification_pending_ = true;
  auto authorization = BindReadAuthorization(generation, std::move(settings));
  owner_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<Core> weak, uint64_t notification,
             IncomingCallback callback, SyncAuthorization authorization) {
            const auto core = weak.lock();
            if (!core)
              return;
            {
              base::AutoLock guard(core->lock_);
              if (notification != core->incoming_notification_id_)
                return;
              core->incoming_notification_pending_ = false;
              if (core->shutting_down_ || core->inbox_persistence_failed_)
                return;
            }
            if (authorization.Run())
              callback.Run(std::move(authorization));
          },
          weak_from_this(), notification, incoming_callback_,
          std::move(authorization)));
}

void CloudKitSyncProviderMac::Core::ReadPendingChanges(
    std::string change_token,
    SyncAuthorization authorization,
    DownloadCallback callback) {
  uint64_t generation;
  {
    base::AutoLock guard(lock_);
    generation = transport_generation_;
  }
  const bool authorized = authorization && authorization.Run();
  {
    base::AutoLock guard(lock_);
    if (!authorized || generation != transport_generation_ ||
        !TransportAllowed()) {
      DispatchDownload(std::move(callback), generation, false, {}, "cancelled");
      return;
    }
    if (download_callback_) {
      DispatchDownload(std::move(callback), generation, false, {},
                       "temporarily_unavailable");
      return;
    }
    HydrateDeferredBookmarks();
    download_base_token_ = std::move(change_token);
    download_error_.clear();
    download_callback_ = std::move(callback);
  }
  // The CloudKit event already fetched and persisted these records. Do not
  // start another fetch, which could turn the receive wake into an echo loop.
  CompleteDownload(nil, generation);
}

bool CloudKitSyncProviderMac::Core::AcknowledgeDownloaded(
    const std::string& change_token,
    SyncAuthorization authorization) {
  uint64_t generation;
  {
    base::AutoLock guard(lock_);
    generation = transport_generation_;
  }
  if (!authorization || !authorization.Run())
    return false;
  base::AutoLock guard(lock_);
  return generation == transport_generation_ && TransportAllowed() &&
         change_token == download_authorization_token_ &&
         DownloadSettingsAuthorized() && AcknowledgeLastDelivery(change_token);
}

SyncAuthorization CloudKitSyncProviderMac::Core::BindReadAuthorization(
    uint64_t generation,
    std::vector<SyncAuthorization> settings) {
  return base::BindRepeating(
      [](std::weak_ptr<Core> weak, uint64_t expected,
         const std::vector<SyncAuthorization>& settings) {
        const auto core = weak.lock();
        if (!core)
          return false;
        base::AutoLock guard(core->lock_);
        if (expected != core->transport_generation_ ||
            !core->TransportAllowed())
          return false;
        for (const auto& scope : settings) {
          if (!scope || !scope.Run())
            return false;
        }
        return true;
      },
      weak_from_this(), generation, std::move(settings));
}

SyncAuthorization CloudKitSyncProviderMac::Core::GetDownloadAuthorization(
    const std::string& token) {
  base::AutoLock guard(lock_);
  return token == download_authorization_token_ ? download_authorization_
                                                : SyncAuthorization();
}

bool CloudKitSyncProviderMac::Core::DownloadSettingsAuthorized() const {
  for (const auto& scope : download_setting_authorizations_) {
    if (!scope || !scope.Run())
      return false;
  }
  return true;
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

void CloudKitSyncProviderMac::Core::AccountSignedInForTesting(
    CKRecordID* current_user) {
  base::AutoLock guard(lock_);
  if (@available(macOS 14.0, *)) {
    HandleAccountChange(CKSyncEngineAccountChangeTypeSignIn, nil, current_user);
  }
}

base::RepeatingCallback<bool()>
CloudKitSyncProviderMac::Core::MakeDelayedRecordDeliveryForTesting(
    CKRecord* record) {
  base::AutoLock guard(lock_);
  const std::string key = ToString(record.recordID.recordName);
  pending_records_[key] = record;
  // A delayed SDK request is only ever for a mutation of the current upload
  // page (UploadKeyAuthorized); model that page as Upload() would.
  const std::optional<EntityType> type =
      EntityTypeForDataClass(record[@"dataClass"]);
  const base::Uuid entity_id = base::Uuid::ParseLowercase(key);
  if (type && entity_id.is_valid()) {
    pending_mutations_[key] = {{.mutation_id = "delayed-delivery-" + key,
                                .entity_type = *type,
                                .entity_id = entity_id}};
  }
  upload_generation_ = transport_generation_;
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
    bool enabled,
    std::string verified_account_record_name) {
  CloudKitSyncConfigurationMac configuration;
  if (!verified_account_record_name.empty()) {
    configuration.verified_account_record_name =
        std::move(verified_account_record_name);
    configuration.verified_key_sha256 = std::string(64, 'a');
    configuration.verified_key_authorization =
        base::BindRepeating([] { return true; });
  }
  auto core =
      std::make_shared<Core>(configuration, path, std::move(cryptor), enabled,
                             base::BindRepeating([] { return true; }));
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

void CloudKitSyncProviderMac::AccountSignedInForTesting(
    CKRecordID* current_user) {
  core_->AccountSignedInForTesting(current_user);
}

base::RepeatingCallback<bool()>
CloudKitSyncProviderMac::MakeDelayedRecordDeliveryForTesting(CKRecord* record) {
  return core_->MakeDelayedRecordDeliveryForTesting(record);
}

}  // namespace ahoi::sync
#pragma clang diagnostic pop
