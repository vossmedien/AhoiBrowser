// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The CloudKit upload path of CloudKitSyncProviderMac::Core: key
// authorization, merge staging against pending records, the modify
// operation and its completion (split from
// cloudkit_sync_provider_mac_consent.mm, source line budget).

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/browser_settings_sync_types.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac_internal.h"

namespace ahoi::sync {

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

}  // namespace ahoi::sync
#pragma clang diagnostic pop
