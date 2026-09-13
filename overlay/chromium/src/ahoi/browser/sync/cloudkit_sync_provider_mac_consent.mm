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
      if (remote)
        StageUploadMergeInput(key, *remote, server->second);
      ++upload_unresolved_count_;
      upload_error_ = "provider_error";
      upload_failure_stage_ = "domain_merge_required";
      return nil;
    }
  }
  return record->second;
}

bool CloudKitSyncProviderMac::Core::UploadKeyAuthorized(
    const std::string& key) const {
  lock_.AssertAcquired();
  if (!TransportAllowed() || operations_cancelling_ ||
      upload_generation_ != transport_generation_)
    return false;
  const auto pending = pending_mutations_.find(key);
  if (pending == pending_mutations_.end() || pending->second.empty())
    return false;
  if (pending->second.front().entity_type == EntityType::kBookmark)
    return BookmarkAllowed();
  if (pending->second.front().entity_type == EntityType::kPermittedSetting) {
    const auto scope = upload_setting_authorizations_.find(key);
    return scope != upload_setting_authorizations_.end() && scope->second &&
           scope->second.Run();
  }
  return true;
}

bool CloudKitSyncProviderMac::Core::StageUploadMergeInput(
    const std::string& key,
    const SyncChange& change,
    CKRecord* encrypted_source) {
  lock_.AssertAcquired();
  SyncRecord incoming;
  if (!UploadKeyAuthorized(key) ||
      change.entity_id.AsLowercaseString() != key ||
      !ValidateChangeEnvelope(change, &incoming))
    return false;
  const auto retained = fetched_changes_.find(key);
  if (retained != fetched_changes_.end()) {
    SyncRecord existing;
    if (!ValidateChangeEnvelope(retained->second, &existing))
      return false;
    if (DomainCovers(existing, incoming) &&
        (!DomainCovers(incoming, existing) ||
         GetVersion(existing) >= GetVersion(incoming))) {
      if (change.entity_type == EntityType::kBookmark && encrypted_source &&
          DomainCovers(incoming, existing)) {
        opaque_bookmark_records_[key] = [encrypted_source copy];
        materialized_bookmark_keys_[key] = key;
      }
      const bool persisted = PersistInbox();
      if (persisted && UploadKeyAuthorized(key))
        ScheduleIncomingNotification();
      return persisted && UploadKeyAuthorized(key);
    }
    if (!DomainCovers(incoming, existing)) {
      // Drain this incomparable retained input first. The untouched outbox or
      // known CKRecord still owns the other input; never replace one with the
      // other or persist a provider-invented merged clock.
      if (PersistInbox() && UploadKeyAuthorized(key))
        ScheduleIncomingNotification();
      return false;
    }
  }
  fetched_changes_[key] = change;
  if (change.entity_type == EntityType::kBookmark) {
    // A category opt-out removes materialized plaintext. Keep the actual
    // encrypted server record as well, so a staged newer remote value cannot
    // fall back to an older opaque record before its domain import completes.
    if (encrypted_source)
      opaque_bookmark_records_[key] = [encrypted_source copy];
    materialized_bookmark_keys_[key] = key;
  }
  const bool persisted = PersistInbox();
  if (persisted && UploadKeyAuthorized(key))
    ScheduleIncomingNotification();
  return persisted && UploadKeyAuthorized(key);
}

bool CloudKitSyncProviderMac::Core::AcknowledgeCoveredMutations(
    const std::string& key,
    const SyncRecord& stored) {
  lock_.AssertAcquired();
  if (!UploadKeyAuthorized(key))
    return false;
  auto found = pending_mutations_.find(key);
  std::set<std::string> covered;
  for (const auto& original : found->second) {
    SyncRecord record;
    if (!upload_expected_mutations_.contains(original.mutation_id) ||
        !ValidateChangeEnvelope(original, &record))
      return false;
    if (DomainCovers(stored, record))
      covered.insert(original.mutation_id);
  }
  if (covered.empty() || !UploadKeyAuthorized(key))
    return false;
  upload_acknowledgements_.insert(covered.begin(), covered.end());
  std::erase_if(found->second, [&](const auto& original) {
    return covered.contains(original.mutation_id);
  });
  if (found->second.empty())
    pending_mutations_.erase(found);
  return true;
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
    if (upload_callback_) {
      DispatchUpload(std::move(callback), generation, false, {},
                     "temporarily_unavailable");
      return;
    }
    if (!TransportAllowed() || shutting_down_ || !engine_ ||
        account_transition_pending_ || zone_recovery_pending_ ||
        operations_cancelling_) {
      DispatchUpload(std::move(callback), generation, false, {},
                     operations_cancelling_ ? "temporarily_unavailable"
                                            : "account_unavailable");
      return;
    }
    struct Original {
      SyncChange change;
      SyncRecord record;
    };
    std::map<std::string, std::vector<Original>> groups;
    std::set<std::string> expected;
    std::map<std::string, SyncAuthorization> setting_authorizations;
    for (const auto& change : changes) {
      if (change.entity_type == EntityType::kBookmark && !BookmarkAllowed()) {
        DispatchUpload(std::move(callback), generation, false, {}, "cancelled");
        return;
      }
      if (change.entity_type == EntityType::kPermittedSetting) {
        auto authorization = GetSettingAuthorization(change.entity_id);
        if (!authorization || !authorization.Run()) {
          DispatchUpload(std::move(callback), generation, false, {},
                         "cancelled");
          return;
        }
        const auto key = change.entity_id.AsLowercaseString();
        const auto previous = setting_authorizations.find(key);
        if (previous != setting_authorizations.end()) {
          authorization = base::BindRepeating(
              [](SyncAuthorization first, SyncAuthorization second) {
                return first && second && first.Run() && second.Run();
              },
              previous->second, std::move(authorization));
        }
        setting_authorizations.insert_or_assign(key, std::move(authorization));
      }
      SyncRecord decoded;
      if (!ValidateChangeEnvelope(change, &decoded)) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "provider_error");
        return;
      }
      if (change.entity_type == EntityType::kPermittedSetting &&
          !IsPortableBrowserSetting(
              std::get<PermittedSettingRecord>(decoded))) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "provider_error");
        return;
      }
      const std::string key = change.entity_id.AsLowercaseString();
      auto& group = groups[key];
      SyncRecord merged;
      if (!expected.insert(change.mutation_id).second ||
          (!group.empty() &&
           MergeRecordFields(group.front().record, decoded, &merged) ==
               MergeDecision::kInvalid)) {
        DispatchUpload(std::move(callback), generation, false, {},
                       "provider_error");
        return;
      }
      group.push_back({change, std::move(decoded)});
    }
    if (groups.empty()) {
      DispatchUpload(std::move(callback), generation, false, {},
                     "provider_error");
      return;
    }
    // Old encoded SDK requests are not authority for a new caller's page.
    // Their exact mutations remain durable in the outbox until proven covered.
    pending_records_.clear();
    pending_mutations_.clear();
    for (const auto& [key, group] : groups)
      for (const auto& original : group)
        pending_mutations_[key].push_back(original.change);
    upload_generation_ = generation;
    upload_expected_mutations_ = std::move(expected);
    upload_setting_authorizations_ = setting_authorizations;
    pending_setting_authorizations_ = std::move(setting_authorizations);
    upload_callback_ = std::move(callback);
    upload_acknowledgements_.clear();
    upload_error_.clear();
    upload_saved_count_ = upload_failed_count_ = upload_resolved_count_ =
        upload_unresolved_count_ = 0;
    upload_item_error_code_ = 0;
    upload_item_error_is_cloudkit_ = false;
    upload_failure_stage_.clear();
    upload_cached_count_ = 0;
    auto fail = [&](const std::string& stage, const std::string& error) {
      upload_failure_stage_ = stage;
      upload_error_ = error;
      ++upload_unresolved_count_;
      LogUploadOutcome(stage, nil);
      DispatchUpload(std::move(upload_callback_), generation, false, {}, error);
    };
    NSMutableArray* pending = [NSMutableArray array];
    std::map<std::string, __strong CKRecord*> records;
    for (const auto& [key, group] : groups) {
      if (!UploadKeyAuthorized(key)) {
        fail("lease_revoked", "cancelled");
        return;
      }
      const Original* selected = &group.front();
      for (const auto& candidate : group) {
        if (DomainCovers(candidate.record, selected->record) &&
            (!DomainCovers(selected->record, candidate.record) ||
             candidate.change.version > selected->change.version))
          selected = &candidate;
      }
      if (!std::ranges::all_of(group, [&](const auto& original) {
            return DomainCovers(selected->record, original.record);
          }))
        selected = nullptr;
      const auto server = server_records_.find(key);
      std::optional<SyncChange> server_change;
      SyncRecord server_record;
      if (server != server_records_.end()) {
        server_change = Decode(server->second);
        if (!server_change ||
            !ValidateChangeEnvelope(*server_change, &server_record)) {
          fail("domain_merge_required", "provider_error");
          return;
        }
        if (std::ranges::all_of(group, [&](const auto& original) {
              return DomainCovers(server_record, original.record);
            })) {
          if (!StageUploadMergeInput(key, *server_change, server->second) ||
              !AcknowledgeCoveredMutations(key, server_record)) {
            fail("persist_newer_remote", "provider_error");
            return;
          }
          ++upload_cached_count_;
          [engine_.state removePendingRecordZoneChanges:@[
            [[CKSyncEnginePendingRecordZoneChange alloc]
                initWithRecordID:server->second.recordID
                            type:
                                CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]
          ]];
          continue;
        }
      }
      if (!selected) {
        const auto latest = std::ranges::max_element(
            group, {},
            [](const auto& original) { return original.change.version; });
        // Feed an unchanged, still-queued original to the existing domain
        // merge against its materialized local winner. No synthetic remote
        // record, new mutation ID, or provider-authored clock is manufactured.
        for (const auto& original : group) {
          if (!DomainCovers(latest->record, original.record)) {
            StageUploadMergeInput(key, original.change);
            break;
          }
        }
        fail("domain_merge_required", "provider_error");
        return;
      }
      if (server_change && !DomainCovers(selected->record, server_record)) {
        StageUploadMergeInput(key, *server_change, server->second);
        fail("domain_merge_required", "provider_error");
        return;
      }
      auto sealed = cryptor_->Seal(selected->change.payload);
      if (!sealed) {
        fail("lease_revoked", "account_unavailable");
        return;
      }
      CKRecordID* record_id =
          [[CKRecordID alloc] initWithRecordName:ToNSString(key)
                                          zoneID:zone_id_];
      CKRecord* record = EncodeCloudKitSyncRecord(
          selected->change, *sealed, record_id,
          server == server_records_.end() ? nil : server->second);
      if (!record || !UploadKeyAuthorized(key)) {
        fail("lease_revoked", "cancelled");
        return;
      }
      records[key] = record;
      [pending
          addObject:
              [[CKSyncEnginePendingRecordZoneChange alloc]
                  initWithRecordID:record_id
                              type:
                                  CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]];
    }
    if (!pending.count) {
      LogUploadOutcome("cached_covered", nil);
      DispatchUpload(
          std::move(upload_callback_), generation, true,
          {upload_acknowledgements_.begin(), upload_acknowledgements_.end()},
          "");
      return;
    }
    pending_records_ = std::move(records);
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

void CloudKitSyncProviderMac::Core::CompleteUpload(NSError* error,
                                                   uint64_t generation) {
  base::AutoLock guard(lock_);
  if (shutting_down_ || generation != transport_generation_ ||
      !upload_callback_) {
    return;
  }
  std::string safe_error =
      !upload_error_.empty() ? upload_error_ : SafeCloudKitError(error);
  const bool fully_resolved =
      !upload_expected_mutations_.empty() &&
      upload_acknowledgements_ == upload_expected_mutations_ &&
      upload_unresolved_count_ == 0 && upload_failed_count_ > 0 &&
      upload_failed_count_ == upload_resolved_count_ && upload_error_.empty() &&
      !inbox_persistence_failed_ && TransportAllowed() &&
      !operations_cancelling_;
  // The aggregate error can retain conflicts already resolved by HandleSent.
  // Normalize only that fully accounted send; any unknown/item/zone failure
  // retains the caller's outbox and normal backoff.
  const bool resolved_partial = fully_resolved &&
                                [error.domain isEqualToString:CKErrorDomain] &&
                                error.code == CKErrorPartialFailure;
  if (resolved_partial) {
    safe_error.clear();
  }
  const bool setting_leases_current = std::ranges::all_of(
      upload_setting_authorizations_,
      [](const auto& entry) { return entry.second && entry.second.Run(); });
  if (!TransportAllowed() || operations_cancelling_ ||
      !setting_leases_current) {
    safe_error = "cancelled";
    upload_failure_stage_ = "lease_revoked";
  }
  if (safe_error.empty() && upload_acknowledgements_.empty()) {
    safe_error = "provider_error";
    upload_failure_stage_ = "empty_ack";
  }
  LogUploadOutcome(safe_error.empty()
                       ? (resolved_partial ? "resolved_partial" : "ok")
                       : (!upload_failure_stage_.empty() ? upload_failure_stage_
                                                         : "send_completion"),
                   error);
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
