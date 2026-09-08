// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "base/functional/bind.h"

namespace ahoi::sync {

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
  std::vector<SyncRecord> records;
  if (store_->GetRecords(EntityType::kPermittedSetting, &records) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  BrowserSettingsProjection result;
  for (const auto& record : records) {
    result.records.push_back(std::get<PermittedSettingRecord>(record));
  }
  result.initial_fetch_complete = store_->HasCompletedInitialFetch();
  result.observed_clock = clock_.last();
  result.authorization = base::BindRepeating(
      [](SyncAuthorization scope,
         std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire) && scope.Run();
      },
      std::move(authority), shared_projection_cancelled_);
  return result;
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
