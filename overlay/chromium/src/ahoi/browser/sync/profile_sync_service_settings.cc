// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_product_settings.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {
namespace {

bool Enabled(const PrefService& prefs, std::string_view id) {
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
  // Restore the HLC from persisted intents before observing another local edit.
  // A retry always reuses the exact payload/clock, never "now" at reconnect.
  for (const auto [id, value] : prefs->GetDict(kBrowserSettingIntentsPref)) {
    if (value.is_string()) {
      auto intent = ReadIntent(id, value.GetString(), local_device_id_);
      if (intent) {
        browser_settings_clock_.Restore(intent->version.stamp);
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
}

void ProfileSyncService::ResetBrowserSettingsWork() {
  browser_settings_cancelled_->store(true, std::memory_order_release);
  browser_settings_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  ++browser_settings_generation_;
  browser_settings_read_pending_ = false;
  browser_settings_read_again_ = false;
  browser_setting_inflight_.clear();
  permitted_settings_seeded_ = false;
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
      if (IsSupportedProductSetting(*profile_->GetPrefs(), id)) {
        allowed.insert(BrowserSettingRecordId(id));
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
  }

  // Drain one durable, coalesced intent at a time. A backend success is SQLite
  // record+outbox acceptance, NOT a claim that PrefService's disk write
  // succeeded. If native queue cleanup is lost, this same record clock makes
  // replay a no-op.
  const auto pending = prefs->GetDict(kBrowserSettingIntentsPref).Clone();
  if (browser_setting_inflight_.empty()) {
    for (const auto [id, value] : pending) {
      if (!Enabled(*prefs, id) || !IsSupportedProductSetting(*prefs, id) ||
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
    base::AutoReset<bool> applying(&applying_product_state_, true);
    for (const auto& record : projection->records) {
      if (!projection->authorization.Run()) {
        break;
      }
      if (record.tombstone || !Enabled(*prefs, record.setting_id) ||
          record.id != BrowserSettingRecordId(record.setting_id) ||
          pending.contains(record.setting_id)) {
        continue;
      }
      if (ApplyPermittedProductSetting(prefs, record.setting_id,
                                       record.value_json)) {
        if (auto actual = EncodePermittedProductSetting(
                *prefs, record.setting_id, true)) {
          observed_user_settings_.insert_or_assign(record.setting_id,
                                                   std::move(*actual));
        }
      }
    }
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
      !profile_->GetPrefs()->IsUserModifiablePreference(setting_id)) {
    return;
  }
  PrefService* prefs = profile_->GetPrefs();
  auto value =
      EncodePermittedProductSetting(*prefs, setting_id, explicit_reset);
  if (!value) {
    return;
  }
  PermittedSettingRecord record{
      .id = BrowserSettingRecordId(setting_id),
      .setting_id = setting_id,
      .value_json = std::move(*value),
      .version = {.stamp = browser_settings_clock_.Tick()}};
  std::string payload;
  if (!SerializeRecord(record, &payload)) {
    return;
  }
  auto pending = prefs->GetDict(kBrowserSettingIntentsPref).Clone();
  pending.Set(setting_id, std::move(payload));
  prefs->SetDict(kBrowserSettingIntentsPref, std::move(pending));
  // Use Chromium's existing preference persistence. Its completion closure has
  // no success result; do not label it a durable-write ACK. Recovery uses the
  // original versioned intent and shared SQLite winner, never fresh defaults.
  prefs->CommitPendingWrite();
  RefreshBrowserSettings();
}

void ProfileSyncService::OnPermittedProductSettingChanged(
    std::string setting_id) {
  if (!profile_ || shutting_down_) {
    return;
  }
  auto current =
      EncodePermittedProductSetting(*profile_->GetPrefs(), setting_id, true);
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
