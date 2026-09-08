// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_extension_setup_controller.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/extension_setup_setting.h"
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

ExtensionRestoreDisposition VerifyNativeReadback(
    const ExtensionDesiredConfiguration& desired,
    const NativeExtensionSetupSnapshot& snapshot) {
  if (!snapshot.complete) {
    return ExtensionRestoreDisposition::kPending;
  }
  std::set<std::string> installed_ids;
  for (const auto& id : snapshot.installed_extension_ids) {
    if (id.size() != 32 ||
        !std::ranges::all_of(id, [](char c) { return c >= 'a' && c <= 'p'; }) ||
        !installed_ids.insert(id).second) {
      return ExtensionRestoreDisposition::kFailed;
    }
  }
  std::set<std::string> eligible_ids;
  const ExtensionDesiredConfiguration* matching = nullptr;
  for (const auto& entry : snapshot.extensions) {
    if (!entry.installed || !IsValidExtensionDesiredConfiguration(entry) ||
        !installed_ids.contains(entry.extension_id) ||
        !eligible_ids.insert(entry.extension_id).second) {
      return ExtensionRestoreDisposition::kFailed;
    }
    if (entry.extension_id == desired.extension_id) {
      matching = &entry;
    }
  }
  // Eligible inventory omits policy/unsupported packages. Only the COMPLETE
  // installed-ID list can attest absence; a hidden managed install still
  // counts.
  const bool matches = desired.installed
                           ? matching && *matching == desired
                           : !installed_ids.contains(desired.extension_id);
  return matches ? ExtensionRestoreDisposition::kApplied
                 : ExtensionRestoreDisposition::kFailed;
}

bool MayStillHaveNativeWork(ExtensionRestoreDisposition disposition) {
  return disposition == ExtensionRestoreDisposition::kPending ||
         disposition == ExtensionRestoreDisposition::kNeedsConfirmation;
}

}  // namespace

struct NativeExtensionSetupController::Operation {
  PermittedSettingRecord record;
  ExtensionRestoreRequest request;
  std::shared_ptr<std::atomic<bool>> cancelled;
  bool awaiting_completion = true;
  bool readback_in_progress = false;
};

NativeExtensionSetupController::NativeExtensionSetupController(
    base::WeakPtr<ProfileSyncUiBridge> bridge,
    ChangedCallback changed)
    : bridge_(std::move(bridge)),
      task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      changed_(std::move(changed)) {}

NativeExtensionSetupController::~NativeExtensionSetupController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_ptr_factory_.InvalidateWeakPtrs();
  for (const auto& [id, operation] : operations_) {
    operation->cancelled->store(true, std::memory_order_release);
  }
  // No owner callback during destruction. Native asynchronous continuations
  // retain only permanently revoked leases, never a pointer to this object.
}

void NativeExtensionSetupController::Request(
    const PermittedSettingRecord& record,
    SyncAuthorization authorization,
    bool user_initiated) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto desired = DecodeExtensionSetupSetting(record);
  std::string revision;
  // SerializeRecord also serves local authoring and can fill an entirely empty
  // map. This incoming boundary must reject it before asking for exact bytes.
  if (!desired || !HasCompleteFieldVersions(record) ||
      !SerializeRecord(record, &revision)) {
    return;
  }
  const std::string extension_id = desired->extension_id;
  const auto previous = operations_.find(extension_id);
  if (previous != operations_.end()) {
    if (previous->second->request.revision == revision) {
      if (!user_initiated) {
        const auto result = results_.find(extension_id);
        if (!previous->second->awaiting_completion &&
            !previous->second->readback_in_progress &&
            result != results_.end() &&
            MayStillHaveNativeWork(result->second.disposition)) {
          RefreshCompletedOperation(previous->second->request);
        }
        return;
      }
    } else {
      SyncRecord merged;
      // The caller supplies the current store's already-merged record. Do not
      // invent a new clock/configuration or regress a field on a stale reply.
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
  ExtensionRestoreRequest request{
      .desired = std::move(*desired),
      .operation_id = base::Uuid::GenerateRandomV4(),
      .revision = std::move(revision),
      .user_initiated = user_initiated,
      .authorization = std::move(lease)};
  ExtensionRestoreResult pending{
      .operation_id = request.operation_id,
      .revision = request.revision,
      .disposition = ExtensionRestoreDisposition::kPending};
  operations_.insert_or_assign(
      extension_id,
      std::make_unique<Operation>(record, request, std::move(cancelled)));
  if (!Authorized(request.authorization)) {
    pending.disposition = ExtensionRestoreDisposition::kCancelled;
    Finish(extension_id, std::move(pending));
    return;
  }
  if (!bridge_) {
    pending.disposition = ExtensionRestoreDisposition::kUnsupported;
    Finish(extension_id, std::move(pending));
    return;
  }

  results_.insert_or_assign(extension_id, pending);
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  Notify(pending);
  if (!lifetime ||
      !IsCurrentOperation(extension_id, request.operation_id,
                          request.revision) ||
      !operations_.find(extension_id)->second->awaiting_completion) {
    return;
  }
  if (!Authorized(request.authorization) || !bridge_) {
    pending.disposition = !bridge_ ? ExtensionRestoreDisposition::kUnsupported
                                   : ExtensionRestoreDisposition::kCancelled;
    Finish(extension_id, std::move(pending));
    return;
  }
  // A default/synchronous bridge callback is posted too. No completion can
  // reenter the controller on another sequence or before this dispatch ends.
  auto completion = base::BindPostTask(
      task_runner_,
      base::BindOnce(&NativeExtensionSetupController::OnNativeResult, lifetime,
                     extension_id, request.operation_id, request.revision));
  bridge_->ApplyNativeExtensionSetup(std::move(request), std::move(completion));
  // The native call may destroy the profile/controller. No access afterwards.
}

void NativeExtensionSetupController::Cancel(std::string_view extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = operations_.find(extension_id);
  if (found == operations_.end() ||
      found->second->cancelled->exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  auto& operation = *found->second;
  operation.awaiting_completion = false;
  ExtensionRestoreResult result{
      .operation_id = operation.request.operation_id,
      .revision = operation.request.revision,
      .disposition = ExtensionRestoreDisposition::kCancelled};
  results_.insert_or_assign(std::string(extension_id), result);
  Notify(std::move(result));
}

void NativeExtensionSetupController::CancelAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::pair<std::string, ExtensionRestoreResult>> notifications;
  for (const auto& [id, operation] : operations_) {
    if (operation->cancelled->exchange(true, std::memory_order_acq_rel)) {
      continue;
    }
    operation->awaiting_completion = false;
    ExtensionRestoreResult result{
        .operation_id = operation->request.operation_id,
        .revision = operation->request.revision,
        .disposition = ExtensionRestoreDisposition::kCancelled};
    results_.insert_or_assign(id, result);
    notifications.emplace_back(id, std::move(result));
  }
  // Revoke every original lease BEFORE callbacks can reenter/start new work.
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  for (auto& [id, result] : notifications) {
    if (!lifetime) {
      return;
    }
    if (IsCurrentOperation(id, result.operation_id, result.revision)) {
      Notify(std::move(result));
    }
  }
}

const std::map<std::string, ExtensionRestoreResult>&
NativeExtensionSetupController::results() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return results_;
}

bool NativeExtensionSetupController::IsCurrentOperation(
    std::string_view extension_id,
    const base::Uuid& operation_id,
    std::string_view revision) const {
  const auto found = operations_.find(extension_id);
  return found != operations_.end() &&
         found->second->request.operation_id == operation_id &&
         found->second->request.revision == revision;
}

void NativeExtensionSetupController::OnNativeResult(
    std::string extension_id,
    base::Uuid expected_operation_id,
    std::string expected_revision,
    ExtensionRestoreResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result.operation_id != expected_operation_id ||
      result.revision != expected_revision ||
      !IsCurrentOperation(extension_id, expected_operation_id,
                          expected_revision) ||
      !operations_.find(extension_id)->second->awaiting_completion) {
    return;
  }
  // Keep copies across foreign native code. Readback/observers can replace the
  // attempt, revoke consent, or destroy the controller and bridge entirely.
  const auto request = operations_.find(extension_id)->second->request;
  if (!Authorized(request.authorization)) {
    result.disposition = ExtensionRestoreDisposition::kCancelled;
  } else if (result.disposition == ExtensionRestoreDisposition::kApplied) {
    if (!bridge_) {
      result.disposition = ExtensionRestoreDisposition::kUnsupported;
    } else {
      const auto lifetime = weak_ptr_factory_.GetWeakPtr();
      auto snapshot = bridge_->ReadNativeExtensionSetup();
      if (!lifetime ||
          !IsCurrentOperation(extension_id, expected_operation_id,
                              expected_revision) ||
          !operations_.find(extension_id)->second->awaiting_completion) {
        return;
      }
      if (!Authorized(request.authorization)) {
        result.disposition = ExtensionRestoreDisposition::kCancelled;
      } else if (!bridge_) {
        result.disposition = ExtensionRestoreDisposition::kUnsupported;
      } else {
        result.disposition = VerifyNativeReadback(request.desired, snapshot);
      }
    }
  }
  Finish(extension_id, std::move(result));
}

void NativeExtensionSetupController::RefreshCompletedOperation(
    ExtensionRestoreRequest request) {
  ExtensionRestoreResult result{.operation_id = request.operation_id,
                                .revision = request.revision};
  if (!Authorized(request.authorization)) {
    result.disposition = ExtensionRestoreDisposition::kCancelled;
  } else if (!bridge_) {
    result.disposition = ExtensionRestoreDisposition::kUnsupported;
  } else {
    // A OnceCallback may have honestly reported a pending download/prompt.
    // Reconcile later native completion without reopening its install flow.
    const auto lifetime = weak_ptr_factory_.GetWeakPtr();
    operations_.find(request.desired.extension_id)
        ->second->readback_in_progress = true;
    const auto snapshot = bridge_->ReadNativeExtensionSetup();
    if (!lifetime ||
        !IsCurrentOperation(request.desired.extension_id, request.operation_id,
                            request.revision)) {
      return;
    }
    auto& operation = *operations_.find(request.desired.extension_id)->second;
    operation.readback_in_progress = false;
    if (operation.cancelled->load(std::memory_order_acquire)) {
      return;  // Reentrant Cancel already published the terminal result.
    }
    if (!Authorized(request.authorization)) {
      result.disposition = ExtensionRestoreDisposition::kCancelled;
    } else if (!bridge_) {
      result.disposition = ExtensionRestoreDisposition::kUnsupported;
    } else {
      result.disposition = VerifyNativeReadback(request.desired, snapshot);
      if (result.disposition != ExtensionRestoreDisposition::kApplied) {
        return;  // Still pending/needs confirmation; no implicit retry.
      }
    }
  }
  Finish(request.desired.extension_id, std::move(result));
}

void NativeExtensionSetupController::Finish(std::string_view extension_id,
                                            ExtensionRestoreResult result) {
  if (!IsCurrentOperation(extension_id, result.operation_id, result.revision)) {
    return;
  }
  auto& operation = *operations_.find(extension_id)->second;
  if (result.disposition == ExtensionRestoreDisposition::kApplied &&
      !Authorized(operation.request.authorization)) {
    result.disposition = ExtensionRestoreDisposition::kCancelled;
  }
  operation.awaiting_completion = false;
  if (!MayStillHaveNativeWork(result.disposition)) {
    operation.cancelled->store(true, std::memory_order_release);
  }
  results_.insert_or_assign(std::string(extension_id), result);
  Notify(std::move(result));
}

void NativeExtensionSetupController::Notify(ExtensionRestoreResult result) {
  const auto callback = changed_;
  if (callback) {
    callback.Run(result);
  }
}

}  // namespace ahoi::sync
