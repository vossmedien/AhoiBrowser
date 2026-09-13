// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_INTERNAL_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_INTERNAL_H_

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_key_bootstrap_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_quarantine.h"
#include "ahoi/browser/sync/cloudkit_sync_record_codec_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

API_AVAILABLE(macos(14.0))
@interface AhoiCloudKitSyncDelegate : NSObject <CKSyncEngineDelegate>
- (instancetype)initWithCore:
    (std::weak_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)core;
- (void)invalidate;
- (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)lockCore;
- (void)completeUpload:(NSError*)error generation:(uint64_t)generation;
- (void)completeDownload:(NSError*)error generation:(uint64_t)generation;
@end
namespace ahoi::sync {
class CloudKitSyncProviderMac::Core
    : public std::enable_shared_from_this<CloudKitSyncProviderMac::Core> {
 public:
  Core(const CloudKitSyncConfigurationMac& configuration,
       base::FilePath state_path,
       std::unique_ptr<SyncPayloadCryptor> cryptor,
       bool bookmark_sync_enabled,
       SyncAuthorization profile_authorization,
       SettingAuthorizationSource setting_authorization = {});
  ~Core();
  void SetBookmarkSyncEnabled(bool enabled);
  SyncAuthorization GetSettingAuthorization(const base::Uuid& id) {
    return setting_authorization_ ? setting_authorization_.Run(id)
                                  : SyncAuthorization();
  }
  void ReceiveRecordForTesting(CKRecord* record);
  base::RepeatingCallback<bool()> MakeDelayedRecordDeliveryForTesting(
      CKRecord* record);
  void ReadCachedChangesForTesting(std::string token,
                                   DownloadCallback callback);
  void AccountChangedForTesting();
  void AccountSignedInForTesting(CKRecordID* current_user);
  void HandleAccountChange(CKSyncEngineAccountChangeType type,
                           CKRecordID* previous_user,
                           CKRecordID* current_user) API_AVAILABLE(macos(14.0));
  void LoadInboxForTesting();

  void Shutdown() {
    __strong CKSyncEngine* engine = nil;
    __strong AhoiCloudKitSyncDelegate* delegate = nil;
    {
      base::AutoLock guard(lock_);
      if (shutting_down_) {
        return;
      }
      shutting_down_ = true;
      engine = engine_;
      delegate = delegate_core_;
      engine_ = nil;
      delegate_core_ = nil;
      upload_callback_.Reset();
      download_callback_.Reset();
      incoming_callback_.Reset();
      ++incoming_notification_id_;
    }

    // Invalidation stops new delegate entries. A callback that already locked
    // the weak Core is serialized by |lock_| and observes |shutting_down_|, so
    // it cannot persist transport state after this method returns.
    [delegate invalidate];
    [engine cancelOperationsWithCompletionHandler:^{
    }];
  }
  bool Initialize() API_AVAILABLE(macos(14.0)) {
    if (!cryptor_ || !owner_runner_ || !profile_authorization_ ||
        !profile_authorization_.Run()) {
      return false;
    }
    NSString* container_id = ToNSString(configuration_.container_identifier);
    CKContainer* container = [CKContainer containerWithIdentifier:container_id];
    if (!container) {
      return false;
    }
    CKDatabase* database = container.privateCloudDatabase;
    zone_id_ = [[CKRecordZoneID alloc]
        initWithZoneName:ToNSString(configuration_.zone_name)
               ownerName:CKCurrentUserDefaultName];
    delegate_core_ =
        [[AhoiCloudKitSyncDelegate alloc] initWithCore:weak_from_this()];
    CKSyncEngineStateSerialization* state;
    {
      base::AutoLock guard(lock_);
      LoadInbox();
      state = LoadState();
      if (persisted_state_invalid_) {
        return false;  // Preserve incompatible/corrupt bytes; no silent reset.
      }
      HydrateDeferredBookmarks();
    }
    CKSyncEngineConfiguration* engine_configuration =
        [[CKSyncEngineConfiguration alloc] initWithDatabase:database
                                         stateSerialization:state
                                                   delegate:delegate_core_];
    engine_configuration.automaticallySync = configuration_.automatically_sync;
    if (!configuration_.subscription_identifier.empty()) {
      engine_configuration.subscriptionID =
          ToNSString(configuration_.subscription_identifier);
    }
    engine_ = [[CKSyncEngine alloc] initWithConfiguration:engine_configuration];
    if (!account_transition_pending_ && !zone_recovery_pending_) {
      CKRecordZone* zone = [[CKRecordZone alloc] initWithZoneID:zone_id_];
      [engine_.state
          addPendingDatabaseChanges:@[ [[CKSyncEnginePendingZoneSave alloc]
                                        initWithZone:zone] ]];
    }
    return engine_ != nil;
  }
  void Upload(std::vector<SyncChange> changes, UploadCallback callback);
  void Download(std::string change_token, DownloadCallback callback);
  void SetIncomingCallback(IncomingCallback callback);
  void ReadPendingChanges(std::string change_token,
                          SyncAuthorization authorization,
                          DownloadCallback callback);
  bool AcknowledgeDownloaded(const std::string& change_token,
                             SyncAuthorization authorization);
  void ScheduleIncomingNotification();
  SyncAuthorization BindReadAuthorization(
      uint64_t generation,
      std::vector<SyncAuthorization> settings);
  SyncAuthorization GetDownloadAuthorization(const std::string& token);
  bool DownloadSettingsAuthorized() const;
  void CompleteUpload(NSError* error, uint64_t generation);
  void CompleteDownload(NSError* error, uint64_t generation);
  void HandleEvent(CKSyncEngineEvent* event) API_AVAILABLE(macos(14.0)) {
    base::AutoLock guard(lock_);
    if (shutting_down_ || !profile_authorization_ ||
        !profile_authorization_.Run()) {
      return;
    }
    switch (event.type) {
      case CKSyncEngineEventTypeStateUpdate: {
        PersistState(event.stateUpdateEvent.stateSerialization);
      } break;
      case CKSyncEngineEventTypeAccountChange: {
        HandleAccountChange(event.accountChangeEvent.changeType,
                            event.accountChangeEvent.previousUser,
                            event.accountChangeEvent.currentUser);
      } break;
      case CKSyncEngineEventTypeFetchedDatabaseChanges:
        for (CKSyncEngineFetchedZoneDeletion* deletion in event
                 .fetchedDatabaseChangesEvent.deletions) {
          if ([deletion.zoneID isEqual:zone_id_]) {
            zone_recovery_pending_ = true;
            PersistInbox();
          }
        }
        break;
      case CKSyncEngineEventTypeFetchedRecordZoneChanges: {
        if (account_transition_pending_ || zone_recovery_pending_) {
          break;
        }
        for (CKSyncEngineFetchedRecordDeletion* deletion in event
                 .fetchedRecordZoneChangesEvent.deletions) {
          if ([deletion.recordID.zoneID isEqual:zone_id_] &&
              ([deletion.recordID.recordName
                   isEqual:@"payload-key-bootstrap-v1"] ||
               [deletion.recordType isEqual:@"AhoiKeyBootstrapClaim"])) {
            key_setup_issue_ = "key_setup_claim_changed";
            ++transport_generation_;
            RequestOperationCancellation();
          }
        }
        for (CKRecord* record in event.fetchedRecordZoneChangesEvent
                 .modifications) {
          ReceiveFetchedRecord(record);
        }
        if (PersistInbox() && !download_callback_ &&
            event.fetchedRecordZoneChangesEvent.modifications.count) {
          ScheduleIncomingNotification();
        }
      }
      // Physical record deletions are not translated into domain deletion;
      // only authenticated tombstones are accepted.
      break;
      case CKSyncEngineEventTypeSentRecordZoneChanges:
        HandleSent(event.sentRecordZoneChangesEvent);
        break;
      case CKSyncEngineEventTypeSentDatabaseChanges:
        if (upload_callback_ &&
            (event.sentDatabaseChangesEvent.failedZoneSaves.count ||
             event.sentDatabaseChangesEvent.failedZoneDeletes.count)) {
          ++upload_unresolved_count_;
          upload_error_ = "provider_error";
          upload_failure_stage_ = "zone_failure";
        }
        break;
      case CKSyncEngineEventTypeDidFetchRecordZoneChanges:
        if (event.didFetchRecordZoneChangesEvent.error) {
          NSError* error = event.didFetchRecordZoneChangesEvent.error;
          download_error_ = SafeCloudKitError(error);
          if (error.code == CKErrorZoneNotFound ||
              error.code == CKErrorUserDeletedZone) {
            zone_recovery_pending_ = true;
            PersistInbox();
          }
        }
        break;
      default:
        break;
    }
  }
  CKSyncEngineRecordZoneChangeBatch* NextBatch(
      CKSyncEngine* engine,
      CKSyncEngineSendChangesContext* context) API_AVAILABLE(macos(14.0)) {
    uint64_t generation;
    NSMutableArray* changes = [NSMutableArray array];
    {
      base::AutoLock guard(lock_);
      if (!TransportAllowed() || shutting_down_ ||
          account_transition_pending_ || zone_recovery_pending_ ||
          operations_cancelling_) {
        return nil;
      }
      generation = transport_generation_;
      for (CKSyncEnginePendingRecordZoneChange* change in engine.state
               .pendingRecordZoneChanges) {
        const auto record =
            pending_records_.find(ToString(change.recordID.recordName));
        if (record != pending_records_.end() &&
            (!IsBookmarkRecord(record->second) || BookmarkAllowed()) &&
            PendingSettingAllowed(record->second) &&
            [context.options.scope containsRecordID:change.recordID] &&
            change.type == CKSyncEnginePendingRecordZoneChangeTypeSaveRecord) {
          [changes addObject:change];
        }
      }
    }
    if (changes.count == 0) {
      return nil;
    }
    std::weak_ptr<Core> weak_core = weak_from_this();
    return [[CKSyncEngineRecordZoneChangeBatch alloc]
        initWithPendingChanges:changes
                recordProvider:^CKRecord*(CKRecordID* record_id) {
                  std::shared_ptr<Core> core = weak_core.lock();
                  if (!core) {
                    return nil;
                  }
                  return core->PendingRecordForGeneration(
                      ToString(record_id.recordName), generation);
                }];
  }
  bool IsAccountTransitionPending() {
    base::AutoLock guard(lock_);
    return !shutting_down_ && account_transition_pending_;
  }
  CKSyncEngineFetchChangesOptions* FetchOptions(
      CKSyncEngineFetchChangesContext* context) API_AVAILABLE(macos(14.0)) {
    base::AutoLock guard(lock_);
    CKSyncEngineFetchChangesOptions* options = [context.options copy];
    options.scope = [[CKSyncEngineFetchChangesScope alloc]
        initWithZoneIDs:zone_id_ && TransportAllowed()
                            ? [NSSet setWithObject:zone_id_]
                            : [NSSet set]];
    return options;
  }
  std::string GetKeySetupIssue() {
    base::AutoLock guard(lock_);
    return key_setup_issue_;
  }
  bool IsBookmarkConsentRevoked() {
    base::AutoLock guard(lock_);
    return bookmark_consent_revoked_;
  }
  BookmarkSyncAuthorization GetBookmarkSyncAuthorization();
  SyncAuthorization GetTransportAuthorization();
  bool IsZoneRecoveryPending() {
    base::AutoLock guard(lock_);
    return !shutting_down_ && zone_recovery_pending_;
  }
  bool ConfirmRecovery(bool account_transition, bool allow_local_upload) {
    base::AutoLock guard(lock_);
    if (shutting_down_ || !engine_ || operations_cancelling_) {
      return false;
    }
    bool& pending = account_transition ? account_transition_pending_
                                       : zone_recovery_pending_;
    if (!pending) {
      return false;
    }
    if (account_transition && base::PathExists(state_path_) &&
        !base::DeleteFile(state_path_)) {
      return false;
    }
    [engine_.state
        removePendingRecordZoneChanges:engine_.state.pendingRecordZoneChanges];
    pending_records_.clear();
    pending_setting_authorizations_.clear();
    server_records_.clear();
    pending_mutations_.clear();
    upload_acknowledgements_.clear();
    pending = false;
    if (!PersistInbox()) {
      pending = true;
      return false;
    }
    if (account_transition_pending_ || zone_recovery_pending_) {
      return true;
    }
    CKRecordZone* zone = [[CKRecordZone alloc] initWithZoneID:zone_id_];
    [engine_.state
        addPendingDatabaseChanges:@[ [[CKSyncEnginePendingZoneSave alloc]
                                      initWithZone:zone] ]];
    (void)allow_local_upload;
    return true;
  }

  bool PersistInboxForTesting() {
    base::AutoLock guard(lock_);
    return !shutting_down_ && PersistInbox();
  }

 private:
  bool PendingSettingAllowed(CKRecord* record) const {
    lock_.AssertAcquired();
    if (EntityTypeForDataClass(record[@"dataClass"]) !=
        EntityType::kPermittedSetting) {
      return true;
    }
    const auto found = pending_setting_authorizations_.find(
        ToString(record.recordID.recordName));
    return found != pending_setting_authorizations_.end() && found->second &&
           found->second.Run();
  }
  static bool IsBookmarkRecord(CKRecord* record) {
    return EntityTypeForDataClass(record[@"dataClass"]) ==
           EntityType::kBookmark;
  }
  static bool SameUploadedPayload(CKRecord* left, CKRecord* right) {
    NSData* left_payload = left.encryptedValues[@"encryptedValue"];
    NSData* right_payload = right.encryptedValues[@"encryptedValue"];
    return [left.recordID isEqual:right.recordID] &&
           [left_payload isKindOfClass:[NSData class]] &&
           [right_payload isKindOfClass:[NSData class]] &&
           [left_payload isEqualToData:right_payload];
  }
  static bool DomainCovers(const SyncRecord& covering,
                           const SyncRecord& original) {
    // Use the domain's complete field clocks, immutable identities, terminal
    // command states and absorbing archive deletion. The envelope topclock or
    // position in an outbox page alone proves none of these properties.
    SyncRecord merged;
    const auto decision = MergeRecordFields(original, covering, &merged);
    return decision == MergeDecision::kAcceptIncoming ||
           decision == MergeDecision::kDuplicate;
  }
  bool UploadKeyAuthorized(const std::string& key) const;
  bool StageUploadMergeInput(const std::string& key,
                             const SyncChange& change,
                             CKRecord* encrypted_source = nil);
  bool AcknowledgeCoveredMutations(const std::string& key,
                                   const SyncRecord& stored);
  bool TransportAllowed() const {
    lock_.AssertAcquired();
    return profile_authorization_ && profile_authorization_.Run() &&
           !shutting_down_ && !persisted_state_invalid_ &&
           key_setup_issue_.empty() &&
           (configuration_.verified_key_sha256.empty() ||
            (configuration_.verified_key_authorization &&
             configuration_.verified_key_authorization.Run())) &&
           !account_transition_pending_ && !zone_recovery_pending_;
  }
  bool BookmarkAllowed() const {
    lock_.AssertAcquired();
    return TransportAllowed() && bookmark_sync_enabled_ &&
           !account_transition_pending_ && !zone_recovery_pending_ &&
           !bookmark_consent_revoked_;
  }
  std::optional<SyncChange> Decode(CKRecord* record) {
    lock_.AssertAcquired();
    if (!TransportAllowed() || !cryptor_ ||
        (IsBookmarkRecord(record) && !BookmarkAllowed())) {
      return std::nullopt;
    }
    return DecodeCloudKitSyncRecord(record, *cryptor_);
  }

  void HandleSent(CKSyncEngineSentRecordZoneChangesEvent* event)
      API_AVAILABLE(macos(14.0)) {
    lock_.AssertAcquired();
    if (!TransportAllowed() || account_transition_pending_ ||
        zone_recovery_pending_ || operations_cancelling_) {
      return;
    }
    for (CKRecord* record in event.savedRecords) {
      if ((IsBookmarkRecord(record) && !BookmarkAllowed()) ||
          !PendingSettingAllowed(record)) {
        continue;
      }
      const std::string key = ToString(record.recordID.recordName);
      const auto attempted = pending_records_.find(key);
      if (attempted == pending_records_.end() ||
          !SameUploadedPayload(record, attempted->second)) {
        // A cancelled older operation must not acknowledge a newer mutation
        // that reused this record ID after category approval changed.
        continue;
      }
      server_records_[key] = record;
      const auto saved = Decode(record);
      SyncRecord stored;
      if (saved && ValidateChangeEnvelope(*saved, &stored) &&
          AcknowledgeCoveredMutations(key, stored)) {
        ++upload_saved_count_;
      } else {
        ++upload_unresolved_count_;
        upload_error_ = "provider_error";
        upload_failure_stage_ = "uncovered_mutation";
        continue;
      }
      pending_records_.erase(key);
      pending_setting_authorizations_.erase(key);
    }
    for (CKSyncEngineFailedRecordSave* failure in event.failedRecordSaves) {
      NSError* error = failure.error;
      ++upload_failed_count_;
      upload_item_error_code_ = error.code;
      upload_item_error_is_cloudkit_ =
          [error.domain isEqualToString:CKErrorDomain];
      CKRecord* server = error.userInfo[CKRecordChangedErrorServerRecordKey];
      const std::string key = ToString(failure.record.recordID.recordName);
      if ((IsBookmarkRecord(failure.record) && !BookmarkAllowed()) ||
          !PendingSettingAllowed(failure.record)) {
        ++upload_unresolved_count_;
        upload_failure_stage_ = "item_lease_revoked";
        if (server && IsBookmarkRecord(server)) {
          ReceiveFetchedRecord(server);
          PersistInbox();
        }
        continue;
      }
      const auto attempted = pending_records_.find(key);
      if (attempted == pending_records_.end() ||
          !SameUploadedPayload(failure.record, attempted->second)) {
        ++upload_unresolved_count_;
        upload_failure_stage_ = "unmatched_item";
        continue;
      }
      if (upload_item_error_is_cloudkit_ &&
          error.code == CKErrorServerRecordChanged && server &&
          [server.recordID isEqual:failure.record.recordID]) {
        server_records_[key] = server;
        std::optional<SyncChange> remote = Decode(server);
        std::optional<SyncChange> local = Decode(failure.record);
        SyncRecord remote_record, local_record;
        const bool valid = remote && local &&
                           ValidateChangeEnvelope(*remote, &remote_record) &&
                           ValidateChangeEnvelope(*local, &local_record);
        // Even an incomparable server result must reach the durable domain
        // merge. Never ACK it merely because its envelope clock is newer.
        if (valid && !StageUploadMergeInput(key, *remote, server)) {
          ++upload_unresolved_count_;
          upload_error_ = "provider_error";
          upload_failure_stage_ = "persist_newer_remote";
          continue;
        }
        if (valid && DomainCovers(remote_record, local_record)) {
          if (AcknowledgeCoveredMutations(key, remote_record)) {
            ++upload_resolved_count_;
          } else {
            ++upload_unresolved_count_;
            upload_failure_stage_ = "unmatched_mutation";
            continue;
          }
          pending_records_.erase(key);
          pending_setting_authorizations_.erase(key);
          // CloudKit did not save this conflict, but the validated server
          // version already satisfies the mutation. Do not leave a phantom
          // save in the engine after acknowledging its outbox entry.
          [engine_.state removePendingRecordZoneChanges:@[
            [[CKSyncEnginePendingRecordZoneChange alloc]
                initWithRecordID:failure.record.recordID
                            type:
                                CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]
          ]];
          continue;
        }
        if (valid) {
          ++upload_unresolved_count_;
          upload_error_ = "provider_error";
          upload_failure_stage_ = "domain_merge_required";
          continue;
        }
      }
      ++upload_unresolved_count_;
      upload_failure_stage_ = "item_failure";
      upload_error_ = SafeCloudKitError(error);
      if (error.code == CKErrorZoneNotFound ||
          error.code == CKErrorUserDeletedZone) {
        zone_recovery_pending_ = true;
        PersistInbox();
      }
    }
    if (event.failedRecordDeletes.count) {
      upload_unresolved_count_ += event.failedRecordDeletes.count;
      upload_error_ = "provider_error";
      upload_failure_stage_ = "unexpected_record_delete";
    }
  }

  void LogUploadOutcome(const std::string& stage, NSError* error) const {
    // Numeric codes stay observable when Foundation redacts dynamic strings.
    int stage_code = 0;
    if (stage == "ok")
      stage_code = 1;
    else if (stage == "resolved_partial")
      stage_code = 2;
    else if (stage == "item_lease_revoked")
      stage_code = 3;
    else if (stage == "unmatched_item")
      stage_code = 4;
    else if (stage == "persist_newer_remote")
      stage_code = 5;
    else if (stage == "unmatched_mutation")
      stage_code = 6;
    else if (stage == "item_failure")
      stage_code = 7;
    else if (stage == "unexpected_record_delete")
      stage_code = 8;
    else if (stage == "zone_failure")
      stage_code = 9;
    else if (stage == "lease_revoked")
      stage_code = 10;
    else if (stage == "empty_ack")
      stage_code = 11;
    else if (stage == "send_completion")
      stage_code = 12;
    else if (stage == "cached_covered")
      stage_code = 13;
    else if (stage == "domain_merge_required")
      stage_code = 14;
    else if (stage == "uncovered_mutation")
      stage_code = 15;
    NSString* domain = !error ? @"none"
                       : [error.domain isEqualToString:CKErrorDomain]
                           ? @"CKErrorDomain"
                           : @"other";
    NSLog(@"AhoiSyncUpload stageCode=%d stage=%@ domain=%@ code=%ld "
           "itemDomain=%@ itemCode=%ld expected=%zu "
           "saved=%zu resolved=%zu cached=%zu unresolved=%zu ack=%zu",
          stage_code, ToNSString(stage), domain, static_cast<long>(error.code),
          upload_item_error_is_cloudkit_ ? @"CKErrorDomain" : @"other",
          static_cast<long>(upload_item_error_code_),
          upload_expected_mutations_.size(), upload_saved_count_,
          upload_resolved_count_, upload_cached_count_,
          upload_unresolved_count_, upload_acknowledgements_.size());
  }

  CKSyncEngineStateSerialization* LoadState() API_AVAILABLE(macos(14.0));
  void PersistState(CKSyncEngineStateSerialization* state)
      API_AVAILABLE(macos(14.0));
  bool AcknowledgeLastDelivery(const std::string& change_token);
  void LoadInbox();
  bool PersistInbox();
  void LoadCachedChange(NSDictionary* item);
  void ReceiveFetchedRecord(CKRecord* record);
  void MaterializeBookmarkRecord(const std::string& key, CKRecord* record);
  void HydrateDeferredBookmarks();
  void ResetAccountState();
  void RequestOperationCancellation();
  CKRecord* PendingRecordForGeneration(const std::string& key,
                                       uint64_t generation);
  void DispatchUpload(UploadCallback callback,
                      uint64_t generation,
                      bool success,
                      std::vector<std::string> acknowledged,
                      std::string error);
  void DispatchDownload(DownloadCallback callback,
                        uint64_t generation,
                        bool success,
                        ProviderBatch batch,
                        std::string error);

  const CloudKitSyncConfigurationMac configuration_;
  const base::FilePath state_path_;
  const base::FilePath inbox_path_;
  std::unique_ptr<SyncPayloadCryptor> cryptor_;
  const SyncAuthorization profile_authorization_;
  const SettingAuthorizationSource setting_authorization_;
  scoped_refptr<base::SequencedTaskRunner> owner_runner_;
  __strong AhoiCloudKitSyncDelegate* delegate_core_ = nil;
  __strong CKSyncEngine* engine_ = nil;
  __strong CKRecordZoneID* zone_id_ = nil;
  std::map<std::string, __strong CKRecord*> pending_records_;
  std::map<std::string, SyncAuthorization> pending_setting_authorizations_;
  std::map<std::string, __strong CKRecord*> server_records_;
  // Every original ID, envelope version and field-clock payload survives
  // coalescing. A SavedRecord can acknowledge only the originals it covers.
  std::map<std::string, std::vector<SyncChange>> pending_mutations_;
  std::map<std::string, SyncChange> fetched_changes_;
  // Keep native encrypted records until their decoded delivery is acknowledged.
  // The persisted cache never grants permission to decrypt them after restart.
  std::map<std::string, __strong CKRecord*> opaque_bookmark_records_;
  std::map<std::string, std::string> materialized_bookmark_keys_;
  std::map<std::string, base::Uuid> bookmark_quarantine_ids_;
  std::set<std::string> upload_acknowledgements_;
  // Includes every original input, not just one ID per physical CKRecord.
  std::set<std::string> upload_expected_mutations_;
  std::map<std::string, SyncAuthorization> upload_setting_authorizations_;
  size_t upload_saved_count_ = 0;
  size_t upload_failed_count_ = 0;
  size_t upload_resolved_count_ = 0;
  size_t upload_cached_count_ = 0;
  size_t upload_unresolved_count_ = 0;
  NSInteger upload_item_error_code_ = 0;
  bool upload_item_error_is_cloudkit_ = false;
  std::string upload_failure_stage_;
  UploadCallback upload_callback_;
  DownloadCallback download_callback_;
  IncomingCallback incoming_callback_;
  SyncAuthorization download_authorization_;
  std::string download_authorization_token_;
  std::vector<SyncAuthorization> download_setting_authorizations_;
  bool incoming_notification_pending_ = false;
  uint64_t incoming_notification_id_ = 0;
  std::string upload_error_;
  std::string download_error_;
  std::string download_base_token_;
  std::string last_delivery_token_;
  std::map<std::string, std::string> last_delivery_mutations_;
  uint64_t download_generation_ = 0;
  uint64_t transport_generation_ = 0;
  uint64_t upload_generation_ = 0;
  bool bookmark_sync_enabled_ = false;
  bool bookmark_consent_revoked_ = false;
  bool operations_cancelling_ = false;
  bool inbox_persistence_failed_ = false;
  bool persisted_state_invalid_ = false;
  bool account_transition_pending_ = false;
  bool zone_recovery_pending_ = false;
  bool shutting_down_ = false;
  std::string key_setup_issue_;
  base::Lock lock_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_INTERNAL_H_
