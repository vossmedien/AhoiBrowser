// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/extension_setup_setting.h"
#include "ahoi/browser/sync/native_extension_setup_controller.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {

bool ProfileSyncService::extension_setup_sync_enabled() const {
  return profile_ && !shutting_down_ &&
         profile_->GetPrefs()->GetBoolean(kExtensionSetupSyncEnabledPref);
}

bool ProfileSyncService::SetExtensionSetupSyncEnabled(bool enabled) {
  if (!profile_ || shutting_down_) {
    return false;
  }
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  profile_->GetPrefs()->SetBoolean(kExtensionSetupSyncEnabledPref, enabled);
  if (!lifetime || shutting_down_) {
    return false;
  }
  InitializeExtensionSetup();
  return true;
}

void ProfileSyncService::InitializeExtensionSetup() {
  if (!profile_ || shutting_down_ || !sync_enabled_ || !ui_bridge_ ||
      extension_setup_controller_) {
    return;
  }
  extension_setup_controller_ =
      std::make_unique<NativeExtensionSetupController>(
          ui_bridge_,
          base::BindRepeating(&ProfileSyncService::OnExtensionRestoreResult,
                              weak_ptr_factory_.GetWeakPtr()));
}

bool ProfileSyncService::PublishNativeExtensionUserIntent(
    ExtensionDesiredConfiguration desired) {
  if (!sync_enabled_ || !extension_setup_sync_enabled() ||
      !IsValidExtensionDesiredConfiguration(desired)) {
    return false;
  }
  // This method is an explicit native USER event boundary, not an inventory
  // diff. A genuine later user choice must cancel an older remote install.
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  if (extension_setup_controller_) {
    extension_setup_controller_->Cancel(desired.extension_id);
  }
  if (!lifetime || shutting_down_ || !extension_setup_sync_enabled()) {
    return false;
  }
  const auto id = ExtensionSetupSettingId(desired.extension_id);
  const auto stored = std::ranges::find(permitted_settings_, id,
                                        &PermittedSettingRecord::setting_id);
  if (stored != permitted_settings_.end() &&
      DecodeExtensionSetupSetting(*stored) == desired &&
      !profile_->GetPrefs()->GetDict(kBrowserSettingIntentsPref).contains(id)) {
    return true;  // Duplicate native user notifications are not new mutations.
  }
  auto record = EncodeExtensionSetupSetting(
      desired, {.stamp = browser_settings_clock_.Tick()});
  if (!record) {
    return false;
  }
  known_extension_setup_ids_.insert(record->id);
  UpdateBrowserSettingConsent();
  return StoreBrowserSettingIntent(std::move(*record));
}

void ProfileSyncService::NotifyNativeExtensionSetupReady() {
  // Native emits this after its real inventory/model becomes complete. It
  // does not authorize any new install or clear a user confirmation boundary.
  permitted_settings_seeded_ = false;
  InitializeExtensionSetup();
  RefreshBrowserSettings();
}

bool ProfileSyncService::RetryExtensionSetup(std::string extension_id) {
  if (!sync_enabled_ || !extension_setup_sync_enabled() || !ui_bridge_) {
    return false;
  }
  const auto id = ExtensionSetupSettingId(extension_id);
  if (!IsExtensionSetupSettingId(id)) {
    return false;
  }
  extension_setup_retry_ = std::move(extension_id);
  RefreshBrowserSettings();  // New provider/account/record lease, no old
                             // request reuse.
  return true;
}

std::map<std::string, ExtensionRestoreResult>
ProfileSyncService::extension_setup_results() const {
  return extension_setup_controller_
             ? extension_setup_controller_->results()
             : std::map<std::string, ExtensionRestoreResult>();
}

void ProfileSyncService::OnExtensionRestoreResult(
    const ExtensionRestoreResult& result) {
  std::ignore = result;
  NotifyObservers();  // Honest native progress only; no automatic retry or
                      // authoring.
}

void ProfileSyncService::ApplyExtensionSetupProjection(
    const BrowserSettingsProjection& projection) {
  if (!extension_setup_sync_enabled() || !sync_enabled_ || !ui_bridge_ ||
      !projection.authorization || !projection.authorization.Run()) {
    return;
  }
  InitializeExtensionSetup();
  if (!extension_setup_controller_) {
    return;
  }
  const auto pending =
      profile_->GetPrefs()->GetDict(kBrowserSettingIntentsPref).Clone();
  const auto retry = std::exchange(extension_setup_retry_, std::nullopt);
  const auto lifetime = weak_ptr_factory_.GetWeakPtr();
  for (const auto& record : projection.records) {
    const auto desired = DecodeExtensionSetupSetting(record);
    const auto current = projection.record_authorizations.find(record.id);
    if (!desired || pending.contains(record.setting_id) ||
        current == projection.record_authorizations.end()) {
      continue;
    }
    auto category = browser_setting_consent_->Capture(record.id);
    if (!category || !category.Run() || !current->second ||
        !current->second.Run()) {
      continue;
    }
    auto valid = base::BindRepeating(
        [](SyncAuthorization original, SyncAuthorization approval) {
          return original.Run() && approval.Run();
        },
        current->second, std::move(category));
    extension_setup_controller_->Request(
        record, std::move(valid), retry && *retry == desired->extension_id);
    if (!lifetime || shutting_down_ || !extension_setup_sync_enabled() ||
        !extension_setup_controller_ || !ui_bridge_) {
      return;
    }
  }

  if (!projection.initial_fetch_complete || !projection.authorization.Run()) {
    return;
  }
  const auto native = ui_bridge_->ReadNativeExtensionSetup();
  if (!lifetime || shutting_down_ || !ui_bridge_ || !native.complete ||
      !projection.authorization.Run()) {
    return;
  }
  std::set<std::string> identities;
  const std::set<std::string> installed(native.installed_extension_ids.begin(),
                                        native.installed_extension_ids.end());
  if (installed.size() != native.installed_extension_ids.size()) {
    return;
  }
  for (const auto& desired : native.extensions) {
    if (!IsValidExtensionDesiredConfiguration(desired) || !desired.installed ||
        !installed.contains(desired.extension_id) ||
        !identities.insert(desired.extension_id).second) {
      return;  // A contradictory capture cannot seed authoritative desired
               // state.
    }
  }
  // Fresh configured A can contribute existing trusted extensions only after
  // fetch. Fresh empty B contributes nothing; absence is never uninstall.
  for (const auto& desired : native.extensions) {
    if (!projection.authorization.Run()) {
      return;
    }
    if (!desired.installed || !IsValidExtensionDesiredConfiguration(desired)) {
      continue;
    }
    const auto id = ExtensionSetupSettingId(desired.extension_id);
    if (pending.contains(id) ||
        std::ranges::find(projection.records, id,
                          &PermittedSettingRecord::setting_id) !=
            projection.records.end()) {
      continue;
    }
    std::ignore = PublishNativeExtensionUserIntent(desired);
    if (!lifetime || shutting_down_) {
      return;
    }
  }
}

}  // namespace ahoi::sync
