// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/browser_setting_catalog.h"
#include "ahoi/browser/sync/extension_setup_setting.h"
#include "ahoi/browser/sync/extension_storage_setting.h"
#include "ahoi/browser/sync/native_extension_setup_controller.h"
#include "ahoi/browser/sync/native_extension_storage_controller.h"
#include "ahoi/browser/sync/native_search_engine_setting.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_product_settings.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {
namespace {

bool Enabled(const PrefService& prefs, std::string_view id) {
  if (IsExtensionStorageSettingId(id)) {
    return prefs.GetBoolean(kExtensionSettingsSyncEnabledPref);
  }
  if (IsExtensionSetupSettingId(id)) {
    return prefs.GetBoolean(kExtensionSetupSyncEnabledPref);
  }
  return std::ranges::any_of(
      prefs.GetList(kPermittedSettingIdsPref), [id](const base::Value& value) {
        return value.is_string() && value.GetString() == id;
      });
}

std::optional<PermittedSettingRecord> ReadIntent(std::string_view id,
                                                 const std::string& payload,
                                                 const base::Uuid& device) {
  SyncRecord decoded;
  if (!DeserializeRecord(EntityType::kPermittedSetting, payload, &decoded)) {
    return std::nullopt;
  }
  auto record = std::get<PermittedSettingRecord>(std::move(decoded));
  if (record.tombstone || record.setting_id != id ||
      record.id != BrowserSettingRecordId(id) ||
      record.version.stamp.device_tiebreak != device.AsLowercaseString()) {
    return std::nullopt;
  }
  return record;
}

}  // namespace

void ProfileSyncService::InitializeBrowserSettings() {
  PrefService* prefs = profile_->GetPrefs();
  InitializeNativeSearchEngineSetting();
  // Restore the HLC from persisted intents before observing another local edit.
  // A retry always reuses the exact payload/clock, never "now" at reconnect.
  for (const auto [id, value] : prefs->GetDict(kBrowserSettingIntentsPref)) {
    if (value.is_string()) {
      auto intent = ReadIntent(id, value.GetString(), local_device_id_);
      if (intent) {
        browser_settings_clock_.Restore(intent->version.stamp);
        if (DecodeExtensionSetupSetting(*intent)) {
          known_extension_setup_ids_.insert(intent->id);
        }
      }
    }
  }
  for (std::string_view id : GetPermittedProductSettingIds()) {
    if (!IsSupportedProductSetting(*prefs, id)) {
      continue;
    }
    if (auto value = EncodePermittedProductSetting(*prefs, id, true)) {
      observed_user_settings_.emplace(id, std::move(*value));
    }
    sync_pref_registrar_.Add(
        std::string(id),
        base::BindRepeating(
            &ProfileSyncService::OnPermittedProductSettingChanged,
            weak_ptr_factory_.GetWeakPtr(), std::string(id)));
  }
  sync_pref_registrar_.Add(
      kPermittedSettingIdsPref,
      base::BindRepeating(&ProfileSyncService::OnBrowserSettingsConsentChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kExtensionSetupSyncEnabledPref,
      base::BindRepeating(&ProfileSyncService::OnBrowserSettingsConsentChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      kExtensionSettingsSyncEnabledPref,
      base::BindRepeating(&ProfileSyncService::OnBrowserSettingsConsentChanged,
                          weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::ResetBrowserSettingsWork() {
  extension_setup_controller_.reset();
  extension_storage_controller_.reset();
  extension_storage_read_pending_ = false;
  extension_storage_read_again_ = false;
  extension_storage_seeded_ = false;
  extension_storage_retry_ = false;
  browser_settings_cancelled_->store(true, std::memory_order_release);
  browser_settings_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  ++browser_settings_generation_;
  browser_settings_read_pending_ = false;
  browser_settings_read_again_ = false;
  browser_setting_inflight_.clear();
  permitted_settings_seeded_ = false;
  extension_setup_retry_.reset();
  // Persisted local intents survive opt-out/shutdown. They cannot publish
  // without the original profile/category scope and the backend account lease.
}

void ProfileSyncService::OnBrowserSettingsConsentChanged() {
  ResetBrowserSettingsWork();
  UpdateBrowserSettingConsent();
  RefreshBrowserSettings();
  NotifyObservers();
}

void ProfileSyncService::UpdateBrowserSettingConsent() {
  std::set<base::Uuid> allowed;
  if (profile_ && sync_enabled_ && !shutting_down_) {
    for (const auto& id : permitted_setting_ids()) {
      if (SupportsBrowserSetting(id)) {
        allowed.insert(BrowserSettingRecordId(id));
      }
    }
    if (extension_setup_sync_enabled()) {
      allowed.insert(known_extension_setup_ids_.begin(),
                     known_extension_setup_ids_.end());
    }
    if (extension_settings_sync_enabled()) {
      for (const auto& descriptor : GetExtensionStorageCatalog()) {
        allowed.insert(BrowserSettingRecordId(ExtensionStorageSettingId(
            descriptor.extension_id, descriptor.key)));
      }
    }
  }
  browser_setting_consent_->SetAllowed(allowed);
}

void ProfileSyncService::RefreshBrowserSettings() {
  if (!profile_ || shutting_down_ || !sync_enabled_ || !backend_ready_ ||
      backend_.is_null()) {
    return;
  }
  if (browser_settings_read_pending_) {
    browser_settings_read_again_ = true;
    return;
  }
  browser_settings_read_pending_ = true;
  backend_.AsyncCall(&ProfileSyncBackend::ReadBrowserSettings)
      .Then(base::BindOnce(&ProfileSyncService::OnBrowserSettingsRead,
                           backend_weak_ptr_factory_.GetWeakPtr(),
                           browser_settings_generation_));
}

void ProfileSyncService::OnBrowserSettingsRead(
    uint64_t generation,
    std::optional<BrowserSettingsProjection> projection) {
  if (generation != browser_settings_generation_) {
    return;
  }
  browser_settings_read_pending_ = false;
  const bool read_again = std::exchange(browser_settings_read_again_, false);
  if (!profile_ || shutting_down_ || !sync_enabled_ || !projection ||
      !projection->authorization || !projection->authorization.Run()) {
    if (read_again) {
      RefreshBrowserSettings();
    }
    return;
  }
  PrefService* prefs = profile_->GetPrefs();
  browser_settings_clock_.Restore(projection->observed_clock);
  for (const auto& record : projection->records) {
    browser_settings_clock_.Restore(record.version.stamp);
    if (DecodeExtensionSetupSetting(record)) {
      known_extension_setup_ids_.insert(record.id);
    }
  }
  UpdateBrowserSettingConsent();

  // Drain one durable, coalesced intent at a time. A backend success is SQLite
  // record+outbox acceptance, NOT a claim that PrefService's disk write
  // succeeded. If native queue cleanup is lost, this same record clock makes
  // replay a no-op.
  const auto pending = prefs->GetDict(kBrowserSettingIntentsPref).Clone();
  if (browser_setting_inflight_.empty()) {
    for (const auto [id, value] : pending) {
      if (!Enabled(*prefs, id) || !SupportsBrowserSetting(id) ||
          !value.is_string()) {
        continue;
      }
      auto intent = ReadIntent(id, value.GetString(), local_device_id_);
      if (!intent) {
        continue;  // Retain malformed/foreign recovery data; never upload it.
      }
      auto authorization = base::BindRepeating(
          [](SyncAuthorization original,
             std::shared_ptr<std::atomic<bool>> category,
             std::shared_ptr<std::atomic<bool>> profile) {
            return !category->load(std::memory_order_acquire) &&
                   !profile->load(std::memory_order_acquire) && original.Run();
          },
          projection->authorization, browser_settings_cancelled_,
          profile_scope_cancelled_);
      browser_setting_inflight_.emplace(id, value.GetString());
      backend_.AsyncCall(&ProfileSyncBackend::PublishBrowserSettingIntent)
          .WithArgs(std::move(*intent), std::move(authorization))
          .Then(
              base::BindOnce(&ProfileSyncService::OnBrowserSettingIntentStored,
                             backend_weak_ptr_factory_.GetWeakPtr(), generation,
                             std::string(id), value.GetString()));
      break;
    }
  }

  {
    // This guard spans only synchronous PrefService application. A native
    // observer still updates its USER-value observation, but cannot echo it.
    // Native service observers may shut down the profile. Do not leave an
    // AutoReset pointing into a possibly destroyed KeyedService across Apply.
    const bool was_applying = std::exchange(applying_product_state_, true);
    const auto lifetime = weak_ptr_factory_.GetWeakPtr();
    for (const auto& record : projection->records) {
      if (!projection->authorization.Run()) {
        break;
      }
      if (IsExtensionSetupSettingId(record.setting_id) ||
          IsExtensionStorageSettingId(record.setting_id) || record.tombstone ||
          !Enabled(*prefs, record.setting_id) ||
          record.id != BrowserSettingRecordId(record.setting_id) ||
          pending.contains(record.setting_id)) {
        continue;
      }
      const auto record_scope =
          projection->record_authorizations.find(record.id);
      if (record_scope == projection->record_authorizations.end() ||
          !record_scope->second || !record_scope->second.Run()) {
        continue;
      }
      const bool applied =
          ApplyBrowserSetting(record.setting_id, record.value_json);
      if (!lifetime) {
        return;
      }
      if (shutting_down_ || !sync_enabled_ ||
          generation != browser_settings_generation_) {
        applying_product_state_ = was_applying;
        return;
      }
      if (applied) {
        if (auto actual = ReadBrowserSetting(record.setting_id, true)) {
          observed_user_settings_.insert_or_assign(record.setting_id,
                                                   std::move(*actual));
        }
      }
    }
    applying_product_state_ = was_applying;
  }
  const auto after_native = weak_ptr_factory_.GetWeakPtr();
  ApplyExtensionSetupProjection(*projection);
  if (!after_native || shutting_down_ || !sync_enabled_ ||
      generation != browser_settings_generation_) {
    return;
  }
  ApplyExtensionStorageProjection(*projection);
  if (!after_native || shutting_down_ || !sync_enabled_ ||
      generation != browser_settings_generation_) {
    return;
  }

  if (!permitted_settings_seeded_ && projection->initial_fetch_complete &&
      projection->authorization.Run()) {
    permitted_settings_seeded_ = true;
    for (const std::string& id : permitted_setting_ids()) {
      const auto found = std::ranges::find(projection->records, id,
                                           &PermittedSettingRecord::setting_id);
      if (found == projection->records.end() && !pending.contains(id)) {
        PublishPermittedProductSetting(id);  // No USER value means no seed.
      }
    }
  }
  if (read_again) {
    RefreshBrowserSettings();
  }
}

void ProfileSyncService::OnBrowserSettingIntentStored(
    uint64_t generation,
    std::string setting_id,
    std::string original_payload,
    std::optional<SyncStateSnapshot> snapshot) {
  if (generation != browser_settings_generation_ || !profile_ ||
      shutting_down_ || !sync_enabled_) {
    return;
  }
  browser_setting_inflight_.erase(setting_id);
  if (!snapshot) {
    return;  // Retain intent; next real provider/native event retries, no spin.
  }
  PrefService* prefs = profile_->GetPrefs();
  auto pending = prefs->GetDict(kBrowserSettingIntentsPref).Clone();
  const std::string* current = pending.FindString(setting_id);
  if (current && *current == original_payload) {
    pending.Remove(setting_id);
    prefs->SetDict(kBrowserSettingIntentsPref, std::move(pending));
  }
  OnBackendState(
      std::move(snapshot));  // Reads the current winner before apply.
  SyncNow();
}

void ProfileSyncService::PublishPermittedProductSetting(std::string setting_id,
                                                        bool explicit_reset) {
  if (!profile_ || shutting_down_ || !sync_enabled_ ||
      applying_product_state_ || !Enabled(*profile_->GetPrefs(), setting_id) ||
      (setting_id != kBrowserSearchEngineSettingId &&
       !profile_->GetPrefs()->IsUserModifiablePreference(setting_id))) {
    return;
  }
  auto value = ReadBrowserSetting(setting_id, explicit_reset);
  if (!value) {
    return;
  }
  PermittedSettingRecord record{
      .id = BrowserSettingRecordId(setting_id),
      .setting_id = setting_id,
      .value_json = std::move(*value),
      .version = {.stamp = browser_settings_clock_.Tick()}};
  std::ignore = StoreBrowserSettingIntent(std::move(record));
}

bool ProfileSyncService::StoreBrowserSettingIntent(
    PermittedSettingRecord record) {
  if (!profile_ || shutting_down_ || !sync_enabled_ ||
      !IsPortableBrowserSetting(record) ||
      !Enabled(*profile_->GetPrefs(), record.setting_id)) {
    return false;
  }
  PrefService* prefs = profile_->GetPrefs();
  std::string payload;
  if (!SerializeRecord(record, &payload)) {
    return false;
  }
  auto pending = prefs->GetDict(kBrowserSettingIntentsPref).Clone();
  pending.Set(record.setting_id, std::move(payload));
  prefs->SetDict(kBrowserSettingIntentsPref, std::move(pending));
  // Use Chromium's existing preference persistence. Its completion closure has
  // no success result; do not label it a durable-write ACK. Recovery uses the
  // original versioned intent and shared SQLite winner, never fresh defaults.
  prefs->CommitPendingWrite();
  RefreshBrowserSettings();
  return true;
}

void ProfileSyncService::OnPermittedProductSettingChanged(
    std::string setting_id) {
  if (!profile_ || shutting_down_) {
    return;
  }
  auto current = ReadBrowserSetting(setting_id, true);
  if (!current) {
    return;
  }
  const auto old = observed_user_settings_.find(setting_id);
  if (old != observed_user_settings_.end() && old->second == *current) {
    return;  // An effective policy/extension value is not a USER mutation.
  }
  observed_user_settings_.insert_or_assign(setting_id, std::move(*current));
  PublishPermittedProductSetting(std::move(setting_id), true);
}

}  // namespace ahoi::sync
