// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "base/functional/bind.h"

namespace ahoi::sync {

bool ProfileSyncBackend::RefreshBrowserSettingScopes() {
  std::vector<SyncRecord> records;
  if (!store_ || store_->GetRecords(EntityType::kPermittedSetting, &records) !=
                     SyncStore::Result::kOk) {
    for (auto& [id, scope] : browser_setting_scopes_) {
      scope.cancelled->store(true, std::memory_order_release);
    }
    browser_setting_scopes_.clear();
    return false;
  }
  std::set<base::Uuid> present;
  for (const auto& value : records) {
    const auto& record = std::get<PermittedSettingRecord>(value);
    present.insert(record.id);
    auto old = browser_setting_scopes_.find(record.id);
    if (old != browser_setting_scopes_.end() && old->second.record == record) {
      continue;
    }
    if (old != browser_setting_scopes_.end()) {
      old->second.cancelled->store(true, std::memory_order_release);
    }
    browser_setting_scopes_.insert_or_assign(
        record.id, BrowserSettingScope{
                       record, std::make_shared<std::atomic<bool>>(false)});
  }
  for (auto it = browser_setting_scopes_.begin();
       it != browser_setting_scopes_.end();) {
    if (!present.contains(it->first)) {
      it->second.cancelled->store(true, std::memory_order_release);
      it = browser_setting_scopes_.erase(it);
    } else {
      ++it;
    }
  }
  return true;
}

SyncAuthorization ProfileSyncBackend::CaptureBrowserSettingsAuthorization() {
  if (!store_ || !ProfileScopeActive() || !transport_enabled_) {
    return {};
  }
  SyncAuthorization provider_scope;
  if (provider_) {
    provider_scope = provider_->GetTransportAuthorization();
    if (!provider_scope || !provider_scope.Run()) {
      return {};
    }
  }
  return base::BindRepeating(
      [](SyncAuthorization profile, SyncAuthorization provider) {
        return profile && profile.Run() && (!provider || provider.Run());
      },
      profile_authorization_, std::move(provider_scope));
}

std::optional<BrowserSettingsProjection>
ProfileSyncBackend::ReadBrowserSettings() {
  auto authority = CaptureBrowserSettingsAuthorization();
  if (!authority || !authority.Run()) {
    return std::nullopt;
  }
  if (!RefreshBrowserSettingScopes()) {
    return std::nullopt;
  }
  BrowserSettingsProjection result;
  for (const auto& [id, scope] : browser_setting_scopes_) {
    result.records.push_back(scope.record);
    result.record_authorizations.emplace(
        id, base::BindRepeating(
                [](SyncAuthorization account,
                   std::shared_ptr<std::atomic<bool>> cancelled) {
                  return !cancelled->load(std::memory_order_acquire) &&
                         account.Run();
                },
                authority, scope.cancelled));
  }
  result.initial_fetch_complete = store_->HasCompletedInitialFetch();
  result.observed_clock = clock_.last();
  result.authorization = std::move(authority);
  return result;
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::SeedBrowserSetting(
    PermittedSettingRecord record,
    SyncAuthorization authorization) {
  auto current = CaptureBrowserSettingsAuthorization();
  if (!authorization || !authorization.Run() || !current || !current.Run()) {
    return std::nullopt;
  }
  SyncRecord stored;
  const auto found =
      store_->GetRecord(EntityType::kPermittedSetting, record.id, &stored);
  if (found == SyncStore::Result::kOk) {
    return CurrentState();
  }
  if (found != SyncStore::Result::kNotFound) {
    return std::nullopt;
  }
  return PublishBrowserSettingIntent(std::move(record),
                                     std::move(authorization));
}

std::optional<SyncStateSnapshot>
ProfileSyncBackend::PublishBrowserSettingIntent(
    PermittedSettingRecord record,
    SyncAuthorization authorization) {
  auto current = CaptureBrowserSettingsAuthorization();
  if (!authorization || !authorization.Run() || !current || !current.Run() ||
      !IsPortableBrowserSetting(record) ||
      record.version.stamp.device_tiebreak != device_id_.AsLowercaseString() ||
      !ValidateRecord(record)) {
    return std::nullopt;
  }
  auto category = setting_authorization_ ? setting_authorization_.Run(record.id)
                                         : SyncAuthorization();
  if (!category || !category.Run()) {
    return std::nullopt;
  }
  SyncRecord stored;
  const auto found =
      store_->GetRecord(EntityType::kPermittedSetting, record.id, &stored);
  if (found == SyncStore::Result::kOk) {
    const auto& existing = std::get<PermittedSettingRecord>(stored);
    if (existing.setting_id != record.setting_id) {
      return std::nullopt;
    }
    // Includes restart replay after the local preference queue cleanup failed.
    // Do not re-stamp an old intent, even when the current value differs.
    if (existing.version >= record.version) {
      return CurrentState();
    }
  } else if (found != SyncStore::Result::kNotFound) {
    return std::nullopt;
  }
  auto valid = base::BindRepeating(
      [](SyncAuthorization original, SyncAuthorization current,
         SyncAuthorization category) {
        return original.Run() && current.Run() && category.Run();
      },
      std::move(authorization), std::move(current), std::move(category));
  const auto result = store_->PutLocalBatch({record}, valid);
  if (result != SyncStore::Result::kOk &&
      result != SyncStore::Result::kAlreadyApplied) {
    return std::nullopt;
  }
  clock_.Restore(record.version.stamp);
  return CurrentState();
}

}  // namespace ahoi::sync
