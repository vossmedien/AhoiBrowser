// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_prefs.h"

#include "components/sync_preferences/pref_service_syncable.h"

namespace ahoi::http_auth_prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kCredentialMetadata);
}

}  // namespace ahoi::http_auth_prefs
