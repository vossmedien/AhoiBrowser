// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/sync/browser_setting_catalog.h"
#include "ahoi/browser/sync/native_search_engine_setting.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_product_settings.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi::sync {

void ProfileSyncService::InitializeNativeSearchEngineSetting() {
  if (!profile_ || shutting_down_ || !sync_enabled_ ||
      native_search_engine_setting_) {
    return;
  }
  native_search_engine_setting_ = std::make_unique<NativeSearchEngineSetting>(
      profile_, base::BindRepeating(
                    &ProfileSyncService::OnNativeSearchEngineSettingChanged,
                    weak_ptr_factory_.GetWeakPtr()));
  if (auto value = native_search_engine_setting_->Read(true)) {
    observed_user_settings_.insert_or_assign(kBrowserSearchEngineSettingId,
                                             std::move(*value));
  }
}

bool ProfileSyncService::SupportsBrowserSetting(std::string_view id) const {
  if (!profile_ || shutting_down_) {
    return false;
  }
  if (id == kBrowserSearchEngineSettingId) {
    // Load/policy readiness is separate from the user's category approval.
    return profile_->IsRegularProfile() && !profile_->IsOffTheRecord();
  }
  return IsSupportedProductSetting(*profile_->GetPrefs(), id);
}

std::optional<std::string> ProfileSyncService::ReadBrowserSetting(
    std::string_view id,
    bool explicit_reset) const {
  if (!SupportsBrowserSetting(id)) {
    return std::nullopt;
  }
  if (id == kBrowserSearchEngineSettingId) {
    return native_search_engine_setting_
               ? native_search_engine_setting_->Read(explicit_reset)
               : std::nullopt;
  }
  return EncodePermittedProductSetting(*profile_->GetPrefs(), id,
                                       explicit_reset);
}

bool ProfileSyncService::ApplyBrowserSetting(std::string_view id,
                                             std::string_view value_json) {
  if (!SupportsBrowserSetting(id)) {
    return false;
  }
  if (id == kBrowserSearchEngineSettingId) {
    return native_search_engine_setting_ &&
           native_search_engine_setting_->Apply(value_json);
  }
  return ApplyPermittedProductSetting(profile_->GetPrefs(), id, value_json);
}

void ProfileSyncService::OnNativeSearchEngineSettingChanged() {
  auto current = ReadBrowserSetting(kBrowserSearchEngineSettingId, true);
  if (current &&
      !observed_user_settings_.contains(kBrowserSearchEngineSettingId)) {
    // First model load is observation, never a user reset or new choice. Once
    // the shared fetch is complete, normal seeding may contribute a USER value.
    observed_user_settings_.emplace(kBrowserSearchEngineSettingId,
                                    std::move(*current));
    permitted_settings_seeded_ = false;
  } else if (current) {
    OnPermittedProductSettingChanged(kBrowserSearchEngineSettingId);
  }
  // Retry a pending remote choice when load/policy/extension readiness changes.
  // Posted self-apply callbacks compare the updated USER observation and cannot
  // echo after the synchronous apply guard has ended.
  RefreshBrowserSettings();
}

}  // namespace ahoi::sync
