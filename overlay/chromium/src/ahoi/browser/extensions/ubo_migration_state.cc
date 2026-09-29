// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_migration_state.h"

#include <algorithm>
#include <string_view>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

namespace ahoi::extensions {

namespace {

constexpr int kMigrationSchemaVersion = 1;

bool IsLowercaseSha256(std::string_view value) {
  return value.size() == 64 && base::IsStringASCII(value) &&
         std::ranges::all_of(value, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

bool StateEquals(const UboPersistedMigrationState& left,
                 const UboPersistedMigrationState& right) {
  return left.extension_id == right.extension_id &&
         left.version == right.version &&
         left.package_sha256 == right.package_sha256 &&
         left.crx_public_key_sha256 == right.crx_public_key_sha256 &&
         left.install_process_token == right.install_process_token;
}

base::DictValue SerializeState(const UboPersistedMigrationState& state) {
  return base::DictValue()
      .Set("schema_version", kMigrationSchemaVersion)
      .Set("extension_id", state.extension_id)
      .Set("version", state.version.GetString())
      .Set("package_sha256", state.package_sha256)
      .Set("crx_public_key_sha256", state.crx_public_key_sha256)
      .Set("install_process_token", state.install_process_token);
}

std::optional<UboPersistedMigrationState> ParseState(
    const base::DictValue& value) {
  if (value.size() != 6u ||
      value.FindInt("schema_version") != kMigrationSchemaVersion) {
    return std::nullopt;
  }
  const std::string* extension_id = value.FindString("extension_id");
  const std::string* version = value.FindString("version");
  const std::string* package_sha256 = value.FindString("package_sha256");
  const std::string* key_sha256 = value.FindString("crx_public_key_sha256");
  const std::string* process_token = value.FindString("install_process_token");
  UboPersistedMigrationState state{
      .extension_id = extension_id ? *extension_id : std::string(),
      .version = base::Version(version ? *version : std::string()),
      .package_sha256 = package_sha256 ? *package_sha256 : std::string(),
      .crx_public_key_sha256 = key_sha256 ? *key_sha256 : std::string(),
      .install_process_token = process_token ? *process_token : std::string(),
  };
  if (state.extension_id != kUboClassicExtensionId ||
      !state.version.IsValid() || !IsLowercaseSha256(state.package_sha256) ||
      !IsLowercaseSha256(state.crx_public_key_sha256) ||
      state.install_process_token.empty() ||
      state.install_process_token.size() > 64u ||
      !base::IsStringASCII(state.install_process_token)) {
    return std::nullopt;
  }
  return state;
}

}  // namespace

void RegisterUboMigrationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // Intentionally local-only. Browser-process identity and extension migration
  // state must never cross Ahoi CloudKit or Chrome Sync.
  registry->RegisterDictionaryPref(kUboMigrationPref);
}

std::optional<UboPersistedMigrationState> ReadUboPersistedMigrationState(
    const PrefService& prefs) {
  if (!prefs.FindPreference(kUboMigrationPref)) {
    return std::nullopt;
  }
  return ParseState(prefs.GetDict(kUboMigrationPref));
}

bool WriteUboPersistedMigrationState(PrefService* prefs,
                                     const UboAuthorizationState& authorization,
                                     std::string install_process_token) {
  if (!prefs || !prefs->FindPreference(kUboMigrationPref) ||
      prefs->IsManagedPreference(kUboMigrationPref) ||
      authorization.extension_id != kUboClassicExtensionId ||
      !authorization.version.IsValid() || install_process_token.empty()) {
    return false;
  }
  UboPersistedMigrationState candidate{
      .extension_id = authorization.extension_id,
      .version = authorization.version,
      .package_sha256 = authorization.package_sha256,
      .crx_public_key_sha256 = authorization.crx_public_key_sha256,
      .install_process_token = std::move(install_process_token),
  };
  prefs->SetDict(kUboMigrationPref, SerializeState(candidate));
  std::optional<UboPersistedMigrationState> readback =
      ReadUboPersistedMigrationState(*prefs);
  if (!readback || !StateEquals(*readback, candidate)) {
    prefs->ClearPref(kUboMigrationPref);
    return false;
  }
  return true;
}

bool UboMigrationMatchesAuthorization(
    const UboPersistedMigrationState& migration,
    const UboAuthorizationState& authorization) {
  return migration.extension_id == authorization.extension_id &&
         migration.version == authorization.version &&
         migration.package_sha256 == authorization.package_sha256 &&
         migration.crx_public_key_sha256 == authorization.crx_public_key_sha256;
}

void ClearUboPersistedMigrationState(PrefService* prefs) {
  if (prefs && prefs->FindPreference(kUboMigrationPref) &&
      !prefs->IsManagedPreference(kUboMigrationPref)) {
    prefs->ClearPref(kUboMigrationPref);
  }
}

}  // namespace ahoi::extensions
