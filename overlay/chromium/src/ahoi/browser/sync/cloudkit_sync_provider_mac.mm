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
#include "ahoi/browser/sync/cloudkit_sync_provider_mac_internal.h"

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

CloudKitSyncProviderMac::Core::Core(
    const CloudKitSyncConfigurationMac& configuration,
    base::FilePath state_path,
    std::unique_ptr<SyncPayloadCryptor> cryptor)
    : configuration_(configuration),
      state_path_(std::move(state_path)),
      inbox_path_(state_path_.AddExtensionASCII("inbox")),
      cryptor_(std::move(cryptor)),
      owner_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}

CloudKitSyncProviderMac::Core::~Core() {
  Shutdown();
}

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
