// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_prefs.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"

namespace ahoi::sync {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kSyncEnabledPref, false);
  registry->RegisterBooleanPref(kBookmarkSyncEnabledPref, false);
  registry->RegisterStringPref(kDeviceIdPref, std::string());
  registry->RegisterStringPref(kDeviceDisplayNamePref, std::string());
  registry->RegisterIntegerPref(kHistoryRetentionDaysPref, 90);
  registry->RegisterListPref(kPermittedSettingIdsPref);
  registry->RegisterListPref(kDeveloperAssetOptInIdsPref);
  registry->RegisterBooleanPref(kRemoteControlEnabledPref, false);
  registry->RegisterDictionaryPref(kApprovedRemoteCommandKeysPref);
}

}  // namespace ahoi::sync
