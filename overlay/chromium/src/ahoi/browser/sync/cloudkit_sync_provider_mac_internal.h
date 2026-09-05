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

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_quarantine.h"
#include "ahoi/browser/sync/cloudkit_sync_record_codec_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"
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
       bool bookmark_sync_enabled);
  ~Core();
  void SetBookmarkSyncEnabled(bool enabled);
  void ReceiveRecordForTesting(CKRecord* record);
  base::RepeatingCallback<bool()> MakeDelayedRecordDeliveryForTesting(
      CKRecord* record);
  void ReadCachedChangesForTesting(std::string token,
                                   DownloadCallback callback);
  void AccountChangedForTesting();
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
    }

    // Invalidation stops new delegate entries. A callback that already locked
    // the weak Core is serialized by |lock_| and observes |shutting_down_|, so
    // it cannot persist transport state after this method returns.
    [delegate invalidate];
    [engine cancelOperationsWithCompletionHandler:^{
    }];
  }
  bool Initialize() API_AVAILABLE(macos(14.0)) {
    if (!cryptor_ || !owner_runner_) {
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
  void CompleteUpload(NSError* error, uint64_t generation);
  void CompleteDownload(NSError* error, uint64_t generation);
  void HandleEvent(CKSyncEngineEvent* event) API_AVAILABLE(macos(14.0)) {
    base::AutoLock guard(lock_);
    if (shutting_down_) {
      return;
    }
    switch (event.type) {
      case CKSyncEngineEventTypeStateUpdate: {
        PersistState(event.stateUpdateEvent.stateSerialization);
      } break;
      case CKSyncEngineEventTypeAccountChange: {
        ResetAccountState();
      } break;
      case CKSyncEngineEventTypeFetchedDatabaseChanges:
        if (event.fetchedDatabaseChangesEvent.deletions.count > 0) {
          zone_recovery_pending_ = true;
          PersistInbox();
        }
        break;
      case CKSyncEngineEventTypeFetchedRecordZoneChanges: {
        if (account_transition_pending_ || zone_recovery_pending_) {
          break;
        }
        for (CKRecord* record in event.fetchedRecordZoneChangesEvent
                 .modifications) {
          ReceiveFetchedRecord(record);
        }
        PersistInbox();
      }
      // Physical record deletions are not translated into domain deletion;
      // only authenticated tombstones are accepted.
      break;
      case CKSyncEngineEventTypeSentRecordZoneChanges:
        HandleSent(event.sentRecordZoneChangesEvent);
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
      if (shutting_down_ || account_transition_pending_ ||
          zone_recovery_pending_ || operations_cancelling_) {
        return nil;
      }
      generation = transport_generation_;
      for (CKSyncEnginePendingRecordZoneChange* change in engine.state
               .pendingRecordZoneChanges) {
        const auto record =
            pending_records_.find(ToString(change.recordID.recordName));
        if (record != pending_records_.end() &&
            (!IsBookmarkRecord(record->second) || BookmarkAllowed()) &&
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
  bool IsBookmarkConsentRevoked() {
    base::AutoLock guard(lock_);
    return bookmark_consent_revoked_;
  }
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
  bool BookmarkAllowed() const {
    lock_.AssertAcquired();
    return bookmark_sync_enabled_ && !account_transition_pending_ &&
           !zone_recovery_pending_ && !bookmark_consent_revoked_;
  }
  std::optional<SyncChange> Decode(CKRecord* record) {
    lock_.AssertAcquired();
    if (!cryptor_ || (IsBookmarkRecord(record) && !BookmarkAllowed())) {
      return std::nullopt;
    }
    return DecodeCloudKitSyncRecord(record, *cryptor_);
  }

  void HandleSent(CKSyncEngineSentRecordZoneChangesEvent* event)
      API_AVAILABLE(macos(14.0)) {
    lock_.AssertAcquired();
    if (account_transition_pending_ || zone_recovery_pending_ ||
        operations_cancelling_) {
      return;
    }
    for (CKRecord* record in event.savedRecords) {
      if (IsBookmarkRecord(record) && !BookmarkAllowed()) {
        continue;
      }
      const std::string key = ToString(record.recordID.recordName);
      const auto attempted = pending_records_.find(key);
      if (attempted != pending_records_.end() &&
          !SameUploadedPayload(record, attempted->second)) {
        // A cancelled older operation must not acknowledge a newer mutation
        // that reused this record ID after category approval changed.
        continue;
      }
      server_records_[key] = record;
      auto mutation = pending_mutations_.find(key);
      if (mutation != pending_mutations_.end()) {
        upload_acknowledgements_.insert(mutation->second);
        pending_mutations_.erase(mutation);
      }
      pending_records_.erase(key);
    }
    for (CKSyncEngineFailedRecordSave* failure in event.failedRecordSaves) {
      NSError* error = failure.error;
      CKRecord* server = error.userInfo[CKRecordChangedErrorServerRecordKey];
      const std::string key = ToString(failure.record.recordID.recordName);
      if (IsBookmarkRecord(failure.record) && !BookmarkAllowed()) {
        if (server && IsBookmarkRecord(server)) {
          ReceiveFetchedRecord(server);
          PersistInbox();
        }
        continue;
      }
      const auto attempted = pending_records_.find(key);
      if (attempted == pending_records_.end() ||
          !SameUploadedPayload(failure.record, attempted->second)) {
        continue;
      }
      if (server) {
        server_records_[key] = server;
        std::optional<SyncChange> remote = Decode(server);
        std::optional<SyncChange> local = Decode(failure.record);
        if (remote && local &&
            (local->version < remote->version ||
             (local->version == remote->version &&
              local->payload == remote->payload))) {
          if (local->version < remote->version) {
            if (IsBookmarkRecord(server)) {
              ReceiveFetchedRecord(server);
            } else {
              fetched_changes_[key] = std::move(*remote);
            }
            PersistInbox();
          }
          auto mutation = pending_mutations_.find(key);
          if (mutation != pending_mutations_.end()) {
            upload_acknowledgements_.insert(mutation->second);
            pending_mutations_.erase(mutation);
          }
          pending_records_.erase(key);
          continue;
        }
      }
      upload_error_ = SafeCloudKitError(error);
      if (error.code == CKErrorZoneNotFound ||
          error.code == CKErrorUserDeletedZone) {
        zone_recovery_pending_ = true;
        PersistInbox();
      }
    }
  }

  CKSyncEngineStateSerialization* LoadState() API_AVAILABLE(macos(14.0));
  void PersistState(CKSyncEngineStateSerialization* state)
      API_AVAILABLE(macos(14.0));
  void AcknowledgeLastDelivery(const std::string& change_token);
  void LoadInbox();
  bool PersistInbox();
  void LoadCachedChange(NSDictionary* item);
  void HydrateLegacyBookmarks();
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
  scoped_refptr<base::SequencedTaskRunner> owner_runner_;
  __strong AhoiCloudKitSyncDelegate* delegate_core_ = nil;
  __strong CKSyncEngine* engine_ = nil;
  __strong CKRecordZoneID* zone_id_ = nil;
  std::map<std::string, __strong CKRecord*> pending_records_;
  std::map<std::string, __strong CKRecord*> server_records_;
  std::map<std::string, std::string> pending_mutations_;
  std::map<std::string, SyncChange> fetched_changes_;
  // Keep native encrypted records until their decoded delivery is acknowledged.
  // The persisted cache never grants permission to decrypt them after restart.
  std::map<std::string, __strong CKRecord*> opaque_bookmark_records_;
  std::map<std::string, __strong NSDictionary*> legacy_bookmark_changes_;
  std::map<std::string, std::string> materialized_bookmark_keys_;
  std::map<std::string, base::Uuid> bookmark_quarantine_ids_;
  std::set<std::string> upload_acknowledgements_;
  UploadCallback upload_callback_;
  DownloadCallback download_callback_;
  std::string upload_error_;
  std::string download_error_;
  std::string download_base_token_;
  std::string last_delivery_token_;
  std::map<std::string, std::string> last_delivery_mutations_;
  uint64_t download_generation_ = 0;
  uint64_t transport_generation_ = 0;
  bool bookmark_sync_enabled_ = false;
  bool bookmark_consent_revoked_ = false;
  bool operations_cancelling_ = false;
  bool inbox_persistence_failed_ = false;
  bool account_transition_pending_ = false;
  bool zone_recovery_pending_ = false;
  bool shutting_down_ = false;
  base::Lock lock_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_INTERNAL_H_
