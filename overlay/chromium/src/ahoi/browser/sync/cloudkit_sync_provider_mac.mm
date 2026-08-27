// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

#include <map>
#include <set>
#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
API_AVAILABLE(macos(14.0))
@interface AhoiCloudKitSyncDelegate : NSObject <CKSyncEngineDelegate>
- (instancetype)initWithCore:
    (std::weak_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)core;
- (void)invalidate;
- (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)lockCore;
- (void)completeUpload:(NSError*)error;
- (void)completeDownload:(NSError*)error;
@end
namespace ahoi::sync {
class CloudKitSyncProviderMac::Core
    : public std::enable_shared_from_this<CloudKitSyncProviderMac::Core> {
 public:
  Core(const CloudKitSyncConfigurationMac& configuration,
       base::FilePath state_path,
       std::unique_ptr<SyncPayloadCryptor> cryptor)
      : configuration_(configuration),
        state_path_(std::move(state_path)),
        inbox_path_(state_path_.AddExtensionASCII("inbox")),
        cryptor_(std::move(cryptor)),
        owner_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}
  ~Core() { Shutdown(); }

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
    LoadInbox();
    CKSyncEngineStateSerialization* state = LoadState();
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
  void Upload(std::vector<SyncChange> changes, UploadCallback callback) {
    {
      base::AutoLock guard(lock_);
      if (shutting_down_ || !engine_ || account_transition_pending_ ||
          zone_recovery_pending_) {
        std::move(callback).Run(false, {}, "account_unavailable");
        return;
      }
    }
    NSMutableArray<CKSyncEnginePendingRecordZoneChange*>* pending =
        [NSMutableArray array];
    for (SyncChange& change : changes) {
      SyncRecord decoded;
      if (!ValidateChangeEnvelope(change, &decoded)) {
        std::move(callback).Run(false, {}, "provider_error");
        return;
      }
      std::optional<std::string> sealed = cryptor_->Seal(change.payload);
      if (!sealed) {
        std::move(callback).Run(false, {}, "account_unavailable");
        return;
      }
      CKRecordID* record_id = [[CKRecordID alloc]
          initWithRecordName:ToNSString(change.entity_id.AsLowercaseString())
                      zoneID:zone_id_];
      CKRecord* record = Encode(change, *sealed, record_id);
      if (!record) {
        std::move(callback).Run(false, {}, "provider_error");
        return;
      }
      const std::string key = change.entity_id.AsLowercaseString();
      {
        base::AutoLock guard(lock_);
        pending_records_[key] = record;
        pending_mutations_[key] = change.mutation_id;
      }
      [pending
          addObject:
              [[CKSyncEnginePendingRecordZoneChange alloc]
                  initWithRecordID:record_id
                              type:
                                  CKSyncEnginePendingRecordZoneChangeTypeSaveRecord]];
    }
    {
      base::AutoLock guard(lock_);
      upload_callback_ = std::move(callback);
      upload_acknowledgements_.clear();
      upload_error_.clear();
    }
    [engine_.state addPendingRecordZoneChanges:pending];
    CKSyncEngineSendChangesScope* scope = [[CKSyncEngineSendChangesScope alloc]
        initWithZoneIDs:[NSSet setWithObject:zone_id_]];
    CKSyncEngineSendChangesOptions* options =
        [[CKSyncEngineSendChangesOptions alloc] initWithScope:scope];
    __weak AhoiCloudKitSyncDelegate* weak_delegate = delegate_core_;
    [engine_ sendChangesWithOptions:options
                  completionHandler:^(NSError* error) {
                    AhoiCloudKitSyncDelegate* strong_delegate = weak_delegate;
                    if (strong_delegate) {
                      [strong_delegate completeUpload:error];
                    }
                  }];
  }
  void Download(std::string change_token, DownloadCallback callback) {
    {
      base::AutoLock guard(lock_);
      if (shutting_down_ || !engine_ || account_transition_pending_ ||
          zone_recovery_pending_) {
        std::move(callback).Run(false, {}, "account_unavailable");
        return;
      }
      AcknowledgeLastDelivery(change_token);
      download_base_token_ = std::move(change_token);
      download_callback_ = std::move(callback);
      download_error_.clear();
    }
    CKSyncEngineFetchChangesScope* scope =
        [[CKSyncEngineFetchChangesScope alloc]
            initWithZoneIDs:[NSSet setWithObject:zone_id_]];
    CKSyncEngineFetchChangesOptions* options =
        [[CKSyncEngineFetchChangesOptions alloc] initWithScope:scope];
    __weak AhoiCloudKitSyncDelegate* weak_delegate = delegate_core_;
    [engine_ fetchChangesWithOptions:options
                   completionHandler:^(NSError* error) {
                     AhoiCloudKitSyncDelegate* strong_delegate = weak_delegate;
                     if (strong_delegate) {
                       [strong_delegate completeDownload:error];
                     }
                   }];
  }
  void CompleteUpload(NSError* error) {
    UploadCallback callback;
    std::string safe_error;
    std::vector<std::string> acknowledged;
    {
      base::AutoLock guard(lock_);
      if (shutting_down_) {
        return;
      }
      callback = std::move(upload_callback_);
      if (!callback) {
        return;
      }
      safe_error =
          !upload_error_.empty() ? upload_error_ : SafeCloudKitError(error);
      acknowledged.assign(upload_acknowledgements_.begin(),
                          upload_acknowledgements_.end());
    }
    owner_runner_->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), safe_error.empty(),
                                  std::move(acknowledged), safe_error));
  }
  void CompleteDownload(NSError* error) {
    DownloadCallback callback;
    std::string safe_error;
    ProviderBatch batch;
    {
      base::AutoLock guard(lock_);
      if (shutting_down_) {
        return;
      }
      callback = std::move(download_callback_);
      if (!callback) {
        return;
      }
      safe_error =
          !download_error_.empty() ? download_error_ : SafeCloudKitError(error);
      if (safe_error.empty()) {
        for (const auto& [id, change] : fetched_changes_) {
          batch.changes.push_back(change);
          last_delivery_mutations_[id] = change.mutation_id;
        }
        if (!batch.changes.empty()) {
          last_delivery_token_ = base::StringPrintf(
              "cksync-%llu",
              static_cast<unsigned long long>(++download_generation_));
          batch.next_change_token = last_delivery_token_;
          PersistInbox();
        } else {
          batch.next_change_token = download_base_token_;
        }
      }
    }
    owner_runner_->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), safe_error.empty(),
                                  std::move(batch), safe_error));
  }
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
        account_transition_pending_ = true;
        fetched_changes_.clear();
        last_delivery_mutations_.clear();
        last_delivery_token_.clear();
        pending_records_.clear();
        server_records_.clear();
        pending_mutations_.clear();
        [engine_.state
            removePendingRecordZoneChanges:engine_.state
                                               .pendingRecordZoneChanges];
        (void)base::DeleteFile(state_path_);
        PersistInbox();
      } break;
      case CKSyncEngineEventTypeFetchedDatabaseChanges:
        if (event.fetchedDatabaseChangesEvent.deletions.count > 0) {
          zone_recovery_pending_ = true;
          PersistInbox();
        }
        break;
      case CKSyncEngineEventTypeFetchedRecordZoneChanges: {
        for (CKRecord* record in event.fetchedRecordZoneChangesEvent
                 .modifications) {
          std::optional<SyncChange> change = Decode(record);
          if (change) {
            fetched_changes_[change->entity_id.AsLowercaseString()] =
                std::move(*change);
          } else {
            std::optional<EntityType> claimed =
                EntityTypeForDataClass(record[@"dataClass"]);
            SyncChange marker = MakeCloudKitQuarantineMarker(
                claimed.value_or(EntityType::kDevice));
            fetched_changes_[marker.entity_id.AsLowercaseString()] =
                std::move(marker);
          }
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
    {
      base::AutoLock guard(lock_);
      if (shutting_down_ || account_transition_pending_ ||
          zone_recovery_pending_) {
        return nil;
      }
    }
    NSMutableArray* changes = [NSMutableArray array];
    for (CKSyncEnginePendingRecordZoneChange* change in engine.state
             .pendingRecordZoneChanges) {
      if ([context.options.scope containsRecordID:change.recordID] &&
          change.type == CKSyncEnginePendingRecordZoneChangeTypeSaveRecord) {
        [changes addObject:change];
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
                  base::AutoLock guard(core->lock_);
                  if (core->shutting_down_) {
                    return nil;
                  }
                  auto record = core->pending_records_.find(
                      ToString(record_id.recordName));
                  return record == core->pending_records_.end()
                             ? nil
                             : record->second;
                }];
  }
  bool IsAccountTransitionPending() {
    base::AutoLock guard(lock_);
    return !shutting_down_ && account_transition_pending_;
  }
  bool IsZoneRecoveryPending() {
    base::AutoLock guard(lock_);
    return !shutting_down_ && zone_recovery_pending_;
  }
  bool ConfirmRecovery(bool account_transition, bool allow_local_upload) {
    base::AutoLock guard(lock_);
    if (shutting_down_ || !engine_) {
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
  CKRecord* Encode(const SyncChange& change,
                   const std::string& sealed,
                   CKRecordID* record_id) API_AVAILABLE(macos(14.0)) {
    CKRecord* server_record = nil;
    {
      base::AutoLock guard(lock_);
      if (shutting_down_) {
        return nil;
      }
      auto server = server_records_.find(change.entity_id.AsLowercaseString());
      if (server != server_records_.end()) {
        server_record = server->second;
      }
    }
    return EncodeCloudKitSyncRecord(change, sealed, record_id, server_record);
  }

  std::optional<SyncChange> Decode(CKRecord* record) {
    return DecodeCloudKitSyncRecord(record, *cryptor_);
  }

  void HandleSent(CKSyncEngineSentRecordZoneChangesEvent* event)
      API_AVAILABLE(macos(14.0)) {
    lock_.AssertAcquired();
    for (CKRecord* record in event.savedRecords) {
      const std::string key = ToString(record.recordID.recordName);
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
      if (server) {
        server_records_[key] = server;
        std::optional<SyncChange> remote = Decode(server);
        std::optional<SyncChange> local = Decode(failure.record);
        if (remote && local &&
            (local->version < remote->version ||
             (local->version == remote->version &&
              local->payload == remote->payload))) {
          if (local->version < remote->version) {
            fetched_changes_[key] = std::move(*remote);
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

  CKSyncEngineStateSerialization* LoadState() API_AVAILABLE(macos(14.0)) {
    NSData* data =
        [NSData dataWithContentsOfFile:ToNSString(state_path_.value())];
    if (!data) {
      return nil;
    }
    NSError* error = nil;
    CKSyncEngineStateSerialization* state = [NSKeyedUnarchiver
        unarchivedObjectOfClass:[CKSyncEngineStateSerialization class]
                       fromData:data
                          error:&error];
    if (!state || error) {
      account_transition_pending_ = zone_recovery_pending_ = true;
      PersistInbox();
      return nil;
    }
    return state;
  }

  void PersistState(CKSyncEngineStateSerialization* state)
      API_AVAILABLE(macos(14.0)) {
    NSError* error = nil;
    NSData* data = [NSKeyedArchiver archivedDataWithRootObject:state
                                         requiringSecureCoding:YES
                                                         error:&error];
    if (!data || error) {
      return;
    }
    base::CreateDirectory(state_path_.DirName());
    [data writeToFile:ToNSString(state_path_.value())
              options:NSDataWritingAtomic
                error:nil];
  }

  void AcknowledgeLastDelivery(const std::string& change_token) {
    if (change_token.empty() || change_token != last_delivery_token_) {
      return;
    }
    for (const auto& [id, mutation_id] : last_delivery_mutations_) {
      auto current = fetched_changes_.find(id);
      if (current != fetched_changes_.end() &&
          current->second.mutation_id == mutation_id) {
        fetched_changes_.erase(current);
      }
    }
    last_delivery_mutations_.clear();
    last_delivery_token_.clear();
    PersistInbox();
  }

  void LoadInbox() {
    NSData* data =
        [NSData dataWithContentsOfFile:ToNSString(inbox_path_.value())];
    if (!data) {
      return;
    }
    NSError* error = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data
                                              options:0
                                                error:&error];
    if (error || ![root isKindOfClass:[NSDictionary class]]) {
      account_transition_pending_ = zone_recovery_pending_ = true;
      return;
    }
    NSDictionary* dictionary = root;
    NSNumber* generation = dictionary[@"generation"];
    if ([generation isKindOfClass:[NSNumber class]]) {
      download_generation_ = generation.unsignedLongLongValue;
    }
    account_transition_pending_ =
        [dictionary[@"accountTransitionPending"] boolValue];
    zone_recovery_pending_ = [dictionary[@"zoneRecoveryPending"] boolValue];
    NSString* delivery_token = dictionary[@"lastDeliveryToken"];
    if ([delivery_token isKindOfClass:[NSString class]]) {
      last_delivery_token_ = ToString(delivery_token);
    }
    NSDictionary* delivered_mutations = dictionary[@"deliveredMutations"];
    if ([delivered_mutations isKindOfClass:[NSDictionary class]]) {
      for (id key in delivered_mutations) {
        id mutation_id = delivered_mutations[key];
        if ([key isKindOfClass:[NSString class]] &&
            [mutation_id isKindOfClass:[NSString class]]) {
          last_delivery_mutations_[ToString(key)] = ToString(mutation_id);
        }
      }
    }
    NSArray* changes = dictionary[@"changes"];
    if (![changes isKindOfClass:[NSArray class]]) {
      return;
    }
    for (id value in changes) {
      if (![value isKindOfClass:[NSDictionary class]]) {
        continue;
      }
      NSDictionary* item = value;
      NSString* mutation_id = item[@"mutationID"];
      NSString* entity_id = item[@"entityID"];
      NSString* device = item[@"device"];
      NSString* payload = item[@"payload"];
      NSNumber* entity_type = item[@"entityType"];
      NSNumber* kind = item[@"kind"];
      NSNumber* model = item[@"model"];
      NSNumber* physical = item[@"physical"];
      NSNumber* logical = item[@"logical"];
      if (![mutation_id isKindOfClass:[NSString class]] ||
          ![entity_id isKindOfClass:[NSString class]] ||
          ![device isKindOfClass:[NSString class]] ||
          ![payload isKindOfClass:[NSString class]] ||
          ![entity_type isKindOfClass:[NSNumber class]] ||
          ![kind isKindOfClass:[NSNumber class]] ||
          ![model isKindOfClass:[NSNumber class]] ||
          ![physical isKindOfClass:[NSNumber class]] ||
          ![logical isKindOfClass:[NSNumber class]]) {
        continue;
      }
      const int raw_type = entity_type.intValue;
      const int raw_kind = kind.intValue;
      if (raw_type < static_cast<int>(EntityType::kDevice) ||
          raw_type > static_cast<int>(EntityType::kDeveloperAsset) ||
          raw_kind < static_cast<int>(ChangeKind::kUpsert) ||
          raw_kind > static_cast<int>(ChangeKind::kDelete)) {
        continue;
      }
      SyncChange change{
          .mutation_id = ToString(mutation_id),
          .entity_type = static_cast<EntityType>(raw_type),
          .entity_id = base::Uuid::ParseLowercase(ToString(entity_id)),
          .kind = static_cast<ChangeKind>(raw_kind),
          .version = {.model_version = model.intValue,
                      .stamp = {.physical_time_us = physical.longLongValue,
                                .logical = logical.unsignedIntValue,
                                .device_tiebreak = ToString(device)}},
          .payload = ToString(payload)};
      SyncRecord decoded;
      if (ValidateChangeEnvelope(change, &decoded) ||
          IsCloudKitQuarantineMarker(change)) {
        fetched_changes_[change.entity_id.AsLowercaseString()] =
            std::move(change);
      }
    }
  }

  bool PersistInbox() {
    NSMutableArray* changes = [NSMutableArray array];
    for (const auto& entry : fetched_changes_) {
      const SyncChange& change = entry.second;
      [changes addObject:@{
        @"mutationID" : ToNSString(change.mutation_id),
        @"entityType" : @(static_cast<int>(change.entity_type)),
        @"entityID" : ToNSString(change.entity_id.AsLowercaseString()),
        @"kind" : @(static_cast<int>(change.kind)),
        @"model" : @(change.version.model_version),
        @"physical" : @(change.version.stamp.physical_time_us),
        @"logical" : @(change.version.stamp.logical),
        @"device" : ToNSString(change.version.stamp.device_tiebreak),
        @"payload" : ToNSString(change.payload),
      }];
    }
    NSMutableDictionary* delivered_mutations = [NSMutableDictionary dictionary];
    for (const auto& [id, mutation_id] : last_delivery_mutations_) {
      delivered_mutations[ToNSString(id)] = ToNSString(mutation_id);
    }
    NSDictionary* root = @{
      @"generation" : @(download_generation_),
      @"accountTransitionPending" : @(account_transition_pending_),
      @"zoneRecoveryPending" : @(zone_recovery_pending_),
      @"lastDeliveryToken" : ToNSString(last_delivery_token_),
      @"deliveredMutations" : delivered_mutations,
      @"changes" : changes,
    };
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:root
                                                   options:0
                                                     error:&error];
    if (!data || error || !base::CreateDirectory(inbox_path_.DirName())) {
      return false;
    }
    return [data writeToFile:ToNSString(inbox_path_.value())
                     options:NSDataWritingAtomic
                       error:nil];
  }

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
  std::set<std::string> upload_acknowledgements_;
  UploadCallback upload_callback_;
  DownloadCallback download_callback_;
  std::string upload_error_;
  std::string download_error_;
  std::string download_base_token_;
  std::string last_delivery_token_;
  std::map<std::string, std::string> last_delivery_mutations_;
  uint64_t download_generation_ = 0;
  bool account_transition_pending_ = false;
  bool zone_recovery_pending_ = false;
  bool shutting_down_ = false;
  base::Lock lock_;
};

}  // namespace ahoi::sync

@implementation AhoiCloudKitSyncDelegate {
  base::Lock _lock;
  std::weak_ptr<ahoi::sync::CloudKitSyncProviderMac::Core> _core;
}
- (instancetype)initWithCore:
    (std::weak_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)core {
  if ((self = [super init])) {
    _core = std::move(core);
  }
  return self;
}
- (void)invalidate {
  base::AutoLock guard(_lock);
  _core.reset();
}
- (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core>)lockCore {
  base::AutoLock guard(_lock);
  return _core.lock();
}
- (void)completeUpload:(NSError*)error {
  if (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core> core =
          [self lockCore]) {
    core->CompleteUpload(error);
  }
}
- (void)completeDownload:(NSError*)error {
  if (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core> core =
          [self lockCore]) {
    core->CompleteDownload(error);
  }
}
- (void)syncEngine:(CKSyncEngine*)syncEngine
       handleEvent:(CKSyncEngineEvent*)event {
  if (std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core> core =
          [self lockCore]) {
    core->HandleEvent(event);
  }
}
- (CKSyncEngineRecordZoneChangeBatch*)syncEngine:(CKSyncEngine*)syncEngine
             nextRecordZoneChangeBatchForContext:
                 (CKSyncEngineSendChangesContext*)context {
  std::shared_ptr<ahoi::sync::CloudKitSyncProviderMac::Core> core =
      [self lockCore];
  return core ? core->NextBatch(syncEngine, context) : nil;
}
- (CKSyncEngineFetchChangesOptions*)syncEngine:(CKSyncEngine*)syncEngine
             nextFetchChangesOptionsForContext:
                 (CKSyncEngineFetchChangesContext*)context {
  return context.options;
}
@end

namespace ahoi::sync {

std::unique_ptr<CloudKitSyncProviderMac> CloudKitSyncProviderMac::Create(
    const CloudKitSyncConfigurationMac& configuration,
    const base::FilePath& state_path,
    std::unique_ptr<SyncPayloadCryptor> cryptor) {
  if (!configuration.IsTransportConfigured() || !cryptor) {
    return nullptr;
  }
  if (@available(macOS 14.0, *)) {
    auto core =
        std::make_shared<Core>(configuration, state_path, std::move(cryptor));
    if (!core->Initialize()) {
      return nullptr;
    }
    return std::unique_ptr<CloudKitSyncProviderMac>(
        new CloudKitSyncProviderMac(std::move(core)));
  }
  return nullptr;
}

CloudKitSyncProviderMac::CloudKitSyncProviderMac(std::shared_ptr<Core> core)
    : core_(std::move(core)) {}

CloudKitSyncProviderMac::~CloudKitSyncProviderMac() {
  core_->Shutdown();
}

// Test-only construction deliberately skips CloudKit and cryptor setup. It
// exercises the same Core shutdown fence used by the production destructor.
std::unique_ptr<CloudKitSyncProviderMac>
CloudKitSyncProviderMac::CreateForTesting(const base::FilePath& state_path) {
  auto core = std::make_shared<Core>(CloudKitSyncConfigurationMac(), state_path,
                                     /*cryptor=*/nullptr);
  return std::unique_ptr<CloudKitSyncProviderMac>(
      new CloudKitSyncProviderMac(std::move(core)));
}

base::RepeatingCallback<bool()>
CloudKitSyncProviderMac::MakeDelayedCacheWriteForTesting() {
  return base::BindRepeating(
      [](std::shared_ptr<Core> core) { return core->PersistInboxForTesting(); },
      core_);
}

void CloudKitSyncProviderMac::Upload(std::vector<SyncChange> changes,
                                     UploadCallback callback) {
  core_->Upload(std::move(changes), std::move(callback));
}

void CloudKitSyncProviderMac::Download(std::string change_token,
                                       DownloadCallback callback) {
  core_->Download(std::move(change_token), std::move(callback));
}

bool CloudKitSyncProviderMac::IsAccountTransitionPending() {
  return core_->IsAccountTransitionPending();
}
bool CloudKitSyncProviderMac::IsZoneRecoveryPending() {
  return core_->IsZoneRecoveryPending();
}
bool CloudKitSyncProviderMac::ConfirmAccountTransition(
    bool allow_local_upload) {
  return core_->ConfirmRecovery(true, allow_local_upload);
}

bool CloudKitSyncProviderMac::ConfirmZoneRecovery() {
  return core_->ConfirmRecovery(false, true);
}
}  // namespace ahoi::sync

#pragma clang diagnostic pop
