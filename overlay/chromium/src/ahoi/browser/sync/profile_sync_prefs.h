// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_PREFS_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ahoi::sync {

// Stable, profile-local identity used by HLC tie-breaking and device-tab
// ownership. It is deliberately not a Chrome account or GAIA identifier.
inline constexpr char kSyncEnabledPref[] = "ahoi.sync.enabled";
// Separate, profile-local consent. A legacy global opt-in does not authorize
// this newly added category, and opening the bookmark shelf never changes it.
inline constexpr char kBookmarkSyncEnabledPref[] =
    "ahoi.sync.bookmarks.enabled";
inline constexpr char kDeviceIdPref[] = "ahoi.sync.device_id";
inline constexpr char kDeviceDisplayNamePref[] =
    "ahoi.sync.device_display_name";
inline constexpr char kHistoryRetentionDaysPref[] =
    "ahoi.sync.history_retention_days";
inline constexpr char kPermittedSettingIdsPref[] =
    "ahoi.sync.permitted_setting_ids";
inline constexpr char kDeveloperAssetOptInIdsPref[] =
    "ahoi.sync.developer_asset_opt_in_ids";
inline constexpr char kRemoteControlEnabledPref[] =
    "ahoi.sync.remote_control.enabled";
inline constexpr char kApprovedRemoteCommandKeysPref[] =
    "ahoi.sync.remote_control.approved_public_keys";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_PREFS_H_
