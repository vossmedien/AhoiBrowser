// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_extension_storage_controller.h"

#include <atomic>
#include <utility>

#include "ahoi/browser/sync/extension_storage_setting.h"
#include "ahoi/browser/sync/profile_sync_ui_bridge.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"

namespace ahoi::sync {
namespace {

bool Authorized(const SyncAuthorization& authorization) {
  return authorization && authorization.Run();
}

}  // namespace

struct NativeExtensionStorageController::Operation {
  PermittedSettingRecord record;
  ExtensionStorageRequest request;
  std::shared_ptr<std::atomic<bool>> cancelled;
  bool awaiting_completion = true;
};

NativeExtensionStorageController::NativeExtensionStorageController(
    base::WeakPtr<ProfileSyncUiBridge> bridge,
    ChangedCallback changed)
    : bridge_(std::move(bridge)),
      task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      changed_(std::move(changed)) {}

NativeExtensionStorageController::~NativeExtensionStorageController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_ptr_factory_.InvalidateWeakPtrs();
  for (const auto& [id, operation] : operations_) {
    operation->cancelled->store(true, std::memory_order_release);
  }
  // Native may retain the lease on its storage sequence. It is permanently
  // revoked before the operation table is destroyed; no owner callback here.
}

void NativeExtensionStorageController::Request(
    const PermittedSettingRecord& record,
    SyncAuthorization authorization,
    bool retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto desired = DecodeExtensionStorageSetting(record);
  std::string revision;
  // SerializeRecord can normalize a freshly authored empty clock map. Do not
  // let that local-authoring convenience weaken this incoming boundary.
  if (!desired || !HasCompleteFieldVersions(record) ||
      !SerializeRecord(record, &revision)) {
    return;
  }
  const std::string setting_id = record.setting_id;
  const auto previous = operations_.find(setting_id);
  if (previous != operations_.end()) {
    if (previous->second->request.revision == revision) {
      if (!retry) {
        return;
      }
    } else {
      SyncRecord merged;
      // A stale projection cannot regress a key. Service supplies the actual
      // merged store record; this controller never invents a merge clock/write.
      if (MergeRecordFields(previous->second->record, record, &merged) !=
          MergeDecision::kAcceptIncoming) {
        return;
      }
    }
    previous->second->cancelled->store(true, std::memory_order_release);
  }

  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  auto lease = base::BindRepeating(
      [](SyncAuthorization original,
         std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire) &&
               Authorized(original);
      },
      std::move(authorization), cancelled);
  ExtensionStorageRequest request{
      .desired = std::move(*desired),
      .operation_id = base::Uuid::GenerateRandomV4(),
      .revision = std::move(revision),
      .authorization = std::move(lease)};
  ExtensionStorageResult pending{
      .operation_id = request.operation_id,
      .revision = request.revision,
      .disposition = ExtensionStorageDisposition::kDeferred};
  operations_.insert_or_assign(
      setting_id,
      std::make_unique<Operation>(record, request, std::move(cancelled)));

  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  bool authorized = Authorized(request.authorization);
  if (!lifetime ||
      !IsPendingOperation(setting_id, request.operation_id, request.revision)) {
    return;
  }
  if (!authorized || !bridge_) {
    pending.disposition = authorized ? ExtensionStorageDisposition::kUnsupported
                                     : ExtensionStorageDisposition::kCancelled;
    Finish(setting_id, std::move(pending));
    return;
  }

  results_.insert_or_assign(setting_id, pending);
  Notify(pending);
  if (!lifetime ||
      !IsPendingOperation(setting_id, request.operation_id, request.revision)) {
    return;
  }
  authorized = Authorized(request.authorization);
  if (!lifetime ||
      !IsPendingOperation(setting_id, request.operation_id, request.revision)) {
    return;
  }
  if (!authorized || !bridge_) {
    pending.disposition = authorized ? ExtensionStorageDisposition::kUnsupported
                                     : ExtensionStorageDisposition::kCancelled;
    Finish(setting_id, std::move(pending));
    return;
  }

  auto completion = base::BindPostTask(
      task_runner_,
      base::BindOnce(&NativeExtensionStorageController::OnNativeResult,
                     lifetime, setting_id, request.operation_id,
                     request.revision));
  bridge_->ApplyNativeExtensionSetting(std::move(request),
                                       std::move(completion));
  // The call may synchronously destroy the bridge/profile/controller. Native
  // completion is posted to our sequence; no member access after dispatch.
}

void NativeExtensionStorageController::Cancel(std::string_view setting_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = operations_.find(setting_id);
  if (found == operations_.end() || !found->second->awaiting_completion) {
    return;
  }
  found->second->cancelled->store(true, std::memory_order_release);
  ExtensionStorageResult result{
      .operation_id = found->second->request.operation_id,
      .revision = found->second->request.revision,
      .disposition = ExtensionStorageDisposition::kCancelled};
  Finish(setting_id, std::move(result));
}

const std::map<std::string, ExtensionStorageResult>&
NativeExtensionStorageController::results() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return results_;
}

bool NativeExtensionStorageController::IsPendingOperation(
    std::string_view setting_id,
    const base::Uuid& operation_id,
    std::string_view revision) const {
  const auto found = operations_.find(setting_id);
  return found != operations_.end() && found->second->awaiting_completion &&
         found->second->request.operation_id == operation_id &&
         found->second->request.revision == revision;
}

void NativeExtensionStorageController::OnNativeResult(
    std::string setting_id,
    base::Uuid expected_operation_id,
    std::string expected_revision,
    ExtensionStorageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result.operation_id != expected_operation_id ||
      result.revision != expected_revision ||
      !IsPendingOperation(setting_id, expected_operation_id,
                          expected_revision)) {
    return;
  }
  Finish(setting_id, std::move(result));
}

void NativeExtensionStorageController::Finish(std::string_view setting_id,
                                              ExtensionStorageResult result) {
  if (!IsPendingOperation(setting_id, result.operation_id, result.revision)) {
    return;
  }
  // Copies protect the check even if foreign authorization code destroys or
  // replaces this operation. No later approval can renew this original lease.
  const auto request = operations_.find(setting_id)->second->request;
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  const bool authorized = Authorized(request.authorization);
  if (!lifetime ||
      !IsPendingOperation(setting_id, result.operation_id, result.revision)) {
    return;
  }
  if (!authorized) {
    result.disposition = ExtensionStorageDisposition::kCancelled;
  } else if (result.disposition == ExtensionStorageDisposition::kStored &&
             (!result.readback ||
              !IsValidExtensionStorageValue(*result.readback) ||
              *result.readback != request.desired)) {
    result.disposition = ExtensionStorageDisposition::kFailed;
  }
  if (result.disposition != ExtensionStorageDisposition::kStored) {
    result.readback.reset();
  }

  auto& operation = *operations_.find(setting_id)->second;
  operation.awaiting_completion = false;
  // Deferred completes this attempt too. Native must not retain a write that
  // can commit behind a subsequent explicit readiness-triggered retry.
  operation.cancelled->store(true, std::memory_order_release);
  results_.insert_or_assign(std::string(setting_id), result);
  Notify(std::move(result));
}

void NativeExtensionStorageController::Notify(ExtensionStorageResult result) {
  const auto callback = changed_;
  if (callback) {
    callback.Run(result);
  }
}

}  // namespace ahoi::sync
