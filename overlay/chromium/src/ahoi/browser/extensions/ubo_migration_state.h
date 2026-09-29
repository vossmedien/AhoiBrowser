// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_MIGRATION_STATE_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_MIGRATION_STATE_H_

#include <optional>
#include <string>

#include "ahoi/browser/extensions/ubo_authorization.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ahoi::extensions {

inline constexpr char kUboMigrationPref[] =
    "ahoi.extensions.ubo.lite_migration";

struct UboPersistedMigrationState {
  std::string extension_id;
  base::Version version;
  std::string package_sha256;
  std::string crx_public_key_sha256;
  std::string install_process_token;
};

void RegisterUboMigrationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);

std::optional<UboPersistedMigrationState> ReadUboPersistedMigrationState(
    const PrefService& prefs);

// Records the exact committed Classic identity and the browser process in
// which it was installed. Lite-removal eligibility is derived at runtime and
// is intentionally not persisted as an irreversible boolean.
bool WriteUboPersistedMigrationState(PrefService* prefs,
                                     const UboAuthorizationState& authorization,
                                     std::string install_process_token);

bool UboMigrationMatchesAuthorization(
    const UboPersistedMigrationState& migration,
    const UboAuthorizationState& authorization);

void ClearUboPersistedMigrationState(PrefService* prefs);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_MIGRATION_STATE_H_
