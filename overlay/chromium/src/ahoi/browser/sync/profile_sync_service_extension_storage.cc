// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/extension_storage_setting.h"
#include "ahoi/browser/sync/native_extension_storage_adapter.h"
#include "ahoi/browser/sync/native_extension_storage_controller.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {

void ProfileSyncService::InitializeExtensionStorage() {
  if (extension_storage_adapter_ || !sync_enabled_ ||
      !extension_settings_sync_enabled()) {
    return;
  }
  extension_storage_adapter_ = std::make_unique<NativeExtensionStorageAdapter>(
      profile_,
      base::BindRepeating(
          [](base::WeakPtr<ProfileSyncService> service,
             ExtensionStorageValue value) {
            if (service) {
              std::ignore = service->PublishNativeExtensionStorageChange(
                  std::move(value));
            }
          },
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          &ProfileSyncService::NotifyNativeExtensionSettingsReady,
          weak_ptr_factory_.GetWeakPtr()));
}

bool ProfileSyncService::extension_settings_sync_enabled() const {
  return profile_ && !shutting_down_ &&
         profile_->GetPrefs()->GetBoolean(kExtensionSettingsSyncEnabledPref);
}

bool ProfileSyncService::SetExtensionSettingsSyncEnabled(bool enabled) {
  if (!profile_ || shutting_down_) {
    return false;
  }
  profile_->GetPrefs()->SetBoolean(kExtensionSettingsSyncEnabledPref, enabled);
  return true;
}

bool ProfileSyncService::PublishNativeExtensionStorageChange(
    ExtensionStorageValue value) {
  if (!sync_enabled_ || !extension_settings_sync_enabled() ||
      !IsValidExtensionStorageValue(value)) {
    return false;
  }
  const auto id = ExtensionStorageSettingId(value.extension_id, value.key);
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  if (extension_storage_controller_) {
    extension_storage_controller_->Cancel(id);
  }
  if (!lifetime || !sync_enabled_ || !extension_settings_sync_enabled()) {
    return false;
  }
  const auto old = std::ranges::find(permitted_settings_, id,
                                     &PermittedSettingRecord::setting_id);
  if (old != permitted_settings_.end() &&
      DecodeExtensionStorageSetting(*old) == value &&
      !profile_->GetPrefs()->GetDict(kBrowserSettingIntentsPref).contains(id)) {
    return true;
  }
  auto record = EncodeExtensionStorageSetting(
      value, {.stamp = browser_settings_clock_.Tick()});
  return record && StoreBrowserSettingIntent(std::move(*record));
}

void ProfileSyncService::NotifyNativeExtensionSettingsReady() {
  extension_storage_seeded_ = false;
  extension_storage_read_again_ = extension_storage_read_pending_;
  extension_storage_retry_ = true;
  RefreshBrowserSettings();  // A native readiness event, never a poll loop.
}

std::map<std::string, ExtensionStorageResult>
ProfileSyncService::extension_settings_results() const {
  return extension_storage_controller_
             ? extension_storage_controller_->results()
             : std::map<std::string, ExtensionStorageResult>();
}

void ProfileSyncService::OnExtensionStorageResult(
    const ExtensionStorageResult& result) {
  std::ignore = result;
  NotifyObservers();  // Storage readback is not provider/remote runtime ACK.
}

void ProfileSyncService::ApplyExtensionStorageProjection(
    const BrowserSettingsProjection& projection) {
  if (!sync_enabled_ || !extension_settings_sync_enabled() ||
      !projection.authorization || !projection.authorization.Run()) {
    return;
  }
  InitializeExtensionStorage();
  if (!extension_storage_controller_) {
    extension_storage_controller_ =
        std::make_unique<NativeExtensionStorageController>(
            extension_storage_adapter_->GetWeakPtr(),
            base::BindRepeating(&ProfileSyncService::OnExtensionStorageResult,
                                weak_ptr_factory_.GetWeakPtr()));
  }
  const auto pending =
      profile_->GetPrefs()->GetDict(kBrowserSettingIntentsPref).Clone();
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  const auto generation = browser_settings_generation_;
  const bool retry = std::exchange(extension_storage_retry_, false);
  for (const auto& record : projection.records) {
    if (!DecodeExtensionStorageSetting(record) ||
        pending.contains(record.setting_id)) {
      continue;
    }
    const auto original = projection.record_authorizations.find(record.id);
    auto category = browser_setting_consent_->Capture(record.id);
    if (original == projection.record_authorizations.end() ||
        !original->second || !original->second.Run() || !category ||
        !category.Run()) {
      continue;
    }
    auto valid = base::BindRepeating(
        [](SyncAuthorization original, SyncAuthorization category) {
          return original.Run() && category.Run();
        },
        original->second, std::move(category));
    extension_storage_controller_->Request(record, std::move(valid), retry);
    if (!lifetime || shutting_down_ ||
        generation != browser_settings_generation_ || !sync_enabled_ ||
        !extension_storage_controller_ || !extension_storage_adapter_) {
      return;
    }
  }
  if (!projection.initial_fetch_complete || extension_storage_seeded_ ||
      extension_storage_read_pending_ || !projection.authorization.Run()) {
    return;
  }
  auto valid = base::BindRepeating(
      [](SyncAuthorization original,
         std::shared_ptr<std::atomic<bool>> category) {
        return !category->load(std::memory_order_acquire) && original.Run();
      },
      projection.authorization, browser_settings_cancelled_);
  extension_storage_read_pending_ = true;
  extension_storage_adapter_->Read(
      valid, base::BindPostTaskToCurrentDefault(
                 base::BindOnce(&ProfileSyncService::OnExtensionStorageRead,
                                lifetime, generation, valid)));
}

void ProfileSyncService::OnExtensionStorageRead(
    uint64_t generation,
    SyncAuthorization authorization,
    NativeExtensionStorageSnapshot snapshot) {
  if (generation != browser_settings_generation_) {
    return;
  }
  extension_storage_read_pending_ = false;
  if (std::exchange(extension_storage_read_again_, false)) {
    RefreshBrowserSettings();  // Discard capture predating the readiness event.
    return;
  }
  if (!sync_enabled_ || !extension_settings_sync_enabled() ||
      !extension_storage_adapter_ || !snapshot.complete || !authorization ||
      !authorization.Run()) {
    return;
  }
  std::set<std::string> captured;
  for (const auto& value : snapshot.values) {
    if (!IsValidExtensionStorageValue(value) ||
        !captured
             .insert(ExtensionStorageSettingId(value.extension_id, value.key))
             .second) {
      return;
    }
  }
  // Every admitted extension capture must cover all of its keys. Incomplete
  // storage reads cannot imply default resets or author partial seed state.
  for (const auto& value : snapshot.values) {
    for (const auto& descriptor : GetExtensionStorageCatalog()) {
      if (descriptor.extension_id == value.extension_id &&
          !captured.contains(ExtensionStorageSettingId(descriptor.extension_id,
                                                       descriptor.key))) {
        return;
      }
    }
  }
  extension_storage_seeded_ = true;
  const auto pending =
      profile_->GetPrefs()->GetDict(kBrowserSettingIntentsPref).Clone();
  for (const auto& value : snapshot.values) {
    const auto id = ExtensionStorageSettingId(value.extension_id, value.key);
    if (!value.value || pending.contains(id) ||
        std::ranges::find(permitted_settings_, id,
                          &PermittedSettingRecord::setting_id) !=
            permitted_settings_.end()) {
      continue;  // Fresh absence is not explicit Remove/reset.
    }
    auto category =
        browser_setting_consent_->Capture(BrowserSettingRecordId(id));
    if (!authorization.Run() || !category || !category.Run()) {
      return;
    }
    auto record = EncodeExtensionStorageSetting(
        value, {.stamp = browser_settings_clock_.Tick()});
    auto valid = base::BindRepeating(
        [](SyncAuthorization original, SyncAuthorization category) {
          return original.Run() && category.Run();
        },
        authorization, std::move(category));
    // Native source already exists durably. Atomic backend absence checking
    // prevents a late capture from overwriting an arriving shared value.
    backend_.AsyncCall(&ProfileSyncBackend::SeedBrowserSetting)
        .WithArgs(std::move(*record), std::move(valid))
        .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                             backend_weak_ptr_factory_.GetWeakPtr()));
  }
}

}  // namespace ahoi::sync
