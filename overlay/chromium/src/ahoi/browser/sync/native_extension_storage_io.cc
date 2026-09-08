// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_extension_storage_io.h"

#include <optional>
#include <set>
#include <utility>
#include <variant>

#include "ahoi/browser/sync/extension_storage_setting.h"
#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "components/sync/base/command_line_switches.h"
#include "components/value_store/value_store.h"
#include "components/value_store/value_store_change.h"
#include "content/public/browser/browser_thread.h"

namespace ahoi::sync {
namespace {

using Disposition = ExtensionStorageDisposition;
using Store = value_store::ValueStore;

std::optional<Disposition> AccessFailure(
    const SyncAuthorization& authorization) {
  if (!authorization || !authorization.Run()) {
    return Disposition::kCancelled;
  }
  // Chromium treats this as presence-only, even if a supplied value is
  // "false". Never race a Google Sync writer for the same extension store.
  if (!base::CommandLine::InitializedForCurrentProcess() ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          syncer::kDisableSync)) {
    return Disposition::kBlockedByPolicy;
  }
  return std::nullopt;
}

bool CleanStatus(const Store::Status& status) {
  return status.ok() && status.restore_status == Store::RESTORE_NONE;
}

Disposition StorageFailure(const Store::Status& status) {
  return status.code == Store::READ_ONLY &&
                 status.restore_status == Store::RESTORE_NONE
             ? Disposition::kBlockedByPolicy
             : Disposition::kFailed;
}

bool ReadSingleValue(const base::DictValue& settings,
                     const std::string& key,
                     std::optional<bool>* value) {
  if (settings.size() > 1 || (!settings.empty() && !settings.contains(key))) {
    return false;
  }
  const base::Value* stored = settings.Find(key);
  if (stored && !stored->is_bool()) {
    return false;  // Stored JSON null is not an absent key/reset either.
  }
  *value = stored ? std::make_optional(stored->GetBool()) : std::nullopt;
  return true;
}

bool ValidRequest(const ExtensionStorageRequest& request) {
  if (!IsValidExtensionStorageValue(request.desired) ||
      !request.operation_id.is_valid() || request.revision.empty()) {
    return false;
  }
  // Controller revisions are the exact canonical format3 record, not a
  // replacement device claim or permission to select another key/value.
  SyncRecord decoded;
  if (!DeserializeRecord(EntityType::kPermittedSetting, request.revision,
                         &decoded)) {
    return false;
  }
  const auto* record = std::get_if<PermittedSettingRecord>(&decoded);
  const auto desired =
      record ? DecodeExtensionStorageSetting(*record) : std::nullopt;
  std::string canonical;
  return desired && *desired == request.desired &&
         SerializeRecord(decoded, &canonical) && canonical == request.revision;
}

bool SameOptionalValue(const std::optional<base::Value>& value,
                       const std::optional<bool>& expected) {
  return value ? expected && value->is_bool() && value->GetBool() == *expected
               : !expected;
}

bool TakeChanges(Store::WriteResult* write,
                 const ExtensionStorageValue& desired,
                 const std::optional<bool>& previous,
                 base::Value* changes) {
  const auto& actual = write->changes();
  if (actual.size() > 1 || (actual.empty() && previous != desired.value)) {
    return false;
  }
  for (const auto& change : actual) {
    if (change.key != desired.key ||
        !SameOptionalValue(change.old_value, previous) ||
        !SameOptionalValue(change.new_value, desired.value)) {
      return false;  // No unexpected key or raw stored value leaves this seam.
    }
  }
  *changes = value_store::ValueStoreChange::ToValue(write->PassChanges());
  return true;
}

NativeExtensionStorageSnapshot ReadOnStorage(
    const std::string& extension_id,
    const std::vector<std::string>& keys,
    const SyncAuthorization& authorization,
    Store* store) {
  if (!store || keys.empty() || AccessFailure(authorization)) {
    return {};
  }
  std::set<std::string, std::less<>> requested;
  for (const auto& key : keys) {
    if (!FindExtensionStorageSetting(extension_id, key) ||
        !requested.insert(key).second) {
      return {};
    }
  }
  if (AccessFailure(authorization)) {
    return {};
  }
  auto read = store->Get(keys);
  if (!CleanStatus(read.status()) || AccessFailure(authorization)) {
    return {};
  }
  for (const auto [key, value] : read.settings()) {
    if (!requested.contains(key) || !value.is_bool()) {
      return {};
    }
  }
  NativeExtensionStorageSnapshot snapshot;
  for (const auto& key : keys) {
    const auto* value = read.settings().Find(key);
    snapshot.values.push_back(
        {.extension_id = extension_id,
         .key = key,
         .value = value ? std::make_optional(value->GetBool()) : std::nullopt});
  }
  if (AccessFailure(authorization)) {
    return {};
  }
  snapshot.complete = true;
  return snapshot;
}

NativeExtensionStorageCommit WriteOnStorage(
    const ExtensionStorageRequest& request,
    Store* store) {
  NativeExtensionStorageCommit commit{
      .result = {.operation_id = request.operation_id,
                 .revision = request.revision,
                 .disposition = Disposition::kFailed}};
  if (auto failure = AccessFailure(request.authorization)) {
    commit.result.disposition = *failure;
    return commit;
  }
  if (!ValidRequest(request)) {
    return commit;
  }
  if (!store) {
    commit.result.disposition = Disposition::kDeferred;
    return commit;
  }
  if (auto failure = AccessFailure(request.authorization)) {
    commit.result.disposition = *failure;
    return commit;
  }
  auto before = store->Get(request.desired.key);
  if (!CleanStatus(before.status())) {
    commit.result.disposition = StorageFailure(before.status());
    return commit;
  }
  std::optional<bool> previous;
  if (!ReadSingleValue(before.settings(), request.desired.key, &previous)) {
    return commit;  // Never overwrite an existing unsupported value type.
  }
  base::Value desired_value(request.desired.value.value_or(false));
  if (auto failure = AccessFailure(request.authorization)) {
    commit.result.disposition = *failure;
    return commit;
  }
  auto write =
      request.desired.value
          ? store->Set(Store::DEFAULTS, request.desired.key, desired_value)
          : store->Remove(request.desired.key);
  if (!write.status().ok()) {
    commit.result.disposition = StorageFailure(write.status());
    return commit;
  }
  if (!TakeChanges(&write, request.desired, previous, &commit.changes) ||
      !CleanStatus(write.status())) {
    return commit;
  }
  // A successful write may already have changed native state. Keep its exact
  // safe changes even if a later revocation/read error forbids a stored claim.
  if (auto failure = AccessFailure(request.authorization)) {
    commit.result.disposition = *failure;
    return commit;
  }
  auto after = store->Get(request.desired.key);
  if (!CleanStatus(after.status())) {
    commit.result.disposition = StorageFailure(after.status());
    return commit;
  }
  std::optional<bool> actual;
  if (!ReadSingleValue(after.settings(), request.desired.key, &actual) ||
      actual != request.desired.value) {
    return commit;
  }
  if (auto failure = AccessFailure(request.authorization)) {
    commit.result.disposition = *failure;
    return commit;
  }
  commit.result.readback =
      ExtensionStorageValue{.extension_id = request.desired.extension_id,
                            .key = request.desired.key,
                            .value = actual};
  commit.result.disposition = Disposition::kStored;
  return commit;
}

}  // namespace

void ReadNativeExtensionStorage(
    std::string extension_id,
    std::vector<std::string> keys,
    SyncAuthorization authorization,
    base::OnceCallback<void(NativeExtensionStorageSnapshot)> callback,
    value_store::ValueStore* store) {
  if (!callback) {
    return;
  }
  auto snapshot = ReadOnStorage(extension_id, keys, authorization, store);
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](SyncAuthorization original,
             base::OnceCallback<void(NativeExtensionStorageSnapshot)> reply,
             NativeExtensionStorageSnapshot snapshot) {
            if (AccessFailure(original)) {
              snapshot = {};
            }
            std::move(reply).Run(std::move(snapshot));
          },
          std::move(authorization), std::move(callback), std::move(snapshot)));
}

void WriteNativeExtensionStorage(
    ExtensionStorageRequest request,
    base::OnceCallback<void(NativeExtensionStorageCommit)> callback,
    value_store::ValueStore* store) {
  if (!callback) {
    return;
  }
  auto commit = WriteOnStorage(request, store);
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](SyncAuthorization original,
             base::OnceCallback<void(NativeExtensionStorageCommit)> reply,
             NativeExtensionStorageCommit commit) {
            if (auto failure = AccessFailure(original)) {
              commit.result.disposition = *failure;
              commit.result.readback.reset();
            }
            std::move(reply).Run(std::move(commit));
          },
          request.authorization, std::move(callback), std::move(commit)));
}

}  // namespace ahoi::sync
