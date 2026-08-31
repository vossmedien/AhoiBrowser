// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_authorization.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "base/base64.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "crypto/hash.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_handlers/manifest_url_handlers.h"

namespace ahoi::extensions {

namespace {

// Schema 2 intentionally invalidates the former fixed-ID authorization state.
// The Official GitHub release key derives a different extension identity.
constexpr int kStateSchemaVersion = 2;
constexpr char kSchemaKey[] = "schema_version";
constexpr char kSequenceKey[] = "sequence";
constexpr char kExtensionIdKey[] = "extension_id";
constexpr char kVersionKey[] = "version";
constexpr char kPackageHashKey[] = "package_sha256";
constexpr char kCrxKeyHashKey[] = "crx_public_key_sha256";
constexpr char kUpdateManifestUrlKey[] = "update_manifest_url";

struct PendingAuthorization {
  uint64_t token = 0;
  UboAuthorizationState state;
};

struct PendingRegistry {
  base::Lock lock;
  uint64_t next_token = 1;
  std::map<const PrefService*, PendingAuthorization> entries;
};

PendingRegistry& GetPendingRegistry() {
  static base::NoDestructor<PendingRegistry> registry;
  return *registry;
}

bool StateEquals(const UboAuthorizationState& lhs,
                 const UboAuthorizationState& rhs) {
  return lhs.sequence == rhs.sequence && lhs.extension_id == rhs.extension_id &&
         lhs.version == rhs.version &&
         lhs.package_sha256 == rhs.package_sha256 &&
         lhs.crx_public_key_sha256 == rhs.crx_public_key_sha256 &&
         lhs.update_manifest_url == rhs.update_manifest_url;
}

std::optional<std::string> PublicKeyHash(
    const ::extensions::Extension& extension) {
  std::optional<std::vector<uint8_t>> decoded =
      base::Base64Decode(extension.public_key());
  if (!decoded) {
    return std::nullopt;
  }
  return base::HexEncodeLower(crypto::hash::Sha256(*decoded));
}

bool ExtensionMatchesState(const ::extensions::Extension& extension,
                           const UboAuthorizationState& state) {
  if (extension.manifest_version() != 2 ||
      extension.GetType() != ::extensions::Manifest::Type::kExtension ||
      extension.location() !=
          ::extensions::mojom::ManifestLocation::kInternal ||
      extension.id() != kUboClassicExtensionId ||
      extension.id() != state.extension_id ||
      extension.version() != state.version) {
    return false;
  }

  // uBO upstream's Chromium manifest has no update_url. Ahoi owns the signed
  // update-manifest URL in browser state; an extension-controlled URL would
  // bypass that policy and is therefore rejected.
  if (!::extensions::ManifestURL::GetUpdateURL(&extension).is_empty()) {
    return false;
  }
  std::optional<std::string> public_key_hash = PublicKeyHash(extension);
  return public_key_hash && *public_key_hash == state.crx_public_key_sha256;
}

bool HasAllowedUpdateContract(const UboCatalogEntry& entry) {
  if (IsPinnedUboBootstrapCatalogEntry(entry)) {
    return entry.update_manifest_url.is_empty();
  }
  return entry.update_manifest_url.is_valid() &&
         entry.update_manifest_url.SchemeIs("https") &&
         !entry.update_manifest_url.has_username() &&
         !entry.update_manifest_url.has_password() &&
         !entry.update_manifest_url.has_ref();
}

base::DictValue SerializeState(const UboAuthorizationState& state) {
  return base::DictValue()
      .Set(kSchemaKey, kStateSchemaVersion)
      .Set(kSequenceKey, base::NumberToString(state.sequence))
      .Set(kExtensionIdKey, state.extension_id)
      .Set(kVersionKey, state.version.GetString())
      .Set(kPackageHashKey, state.package_sha256)
      .Set(kCrxKeyHashKey, state.crx_public_key_sha256)
      .Set(kUpdateManifestUrlKey, state.update_manifest_url.spec());
}

std::optional<UboAuthorizationState> ParseState(const base::DictValue& dict) {
  std::optional<int> schema_version = dict.FindInt(kSchemaKey);
  if (dict.size() != 7u || !schema_version ||
      *schema_version != kStateSchemaVersion) {
    return std::nullopt;
  }
  const std::string* sequence = dict.FindString(kSequenceKey);
  const std::string* extension_id = dict.FindString(kExtensionIdKey);
  const std::string* version = dict.FindString(kVersionKey);
  const std::string* package_hash = dict.FindString(kPackageHashKey);
  const std::string* crx_key_hash = dict.FindString(kCrxKeyHashKey);
  const std::string* update_manifest_url =
      dict.FindString(kUpdateManifestUrlKey);
  UboAuthorizationState state;
  if (!sequence || !base::StringToUint64(*sequence, &state.sequence) ||
      state.sequence == 0 || !extension_id ||
      *extension_id != kUboClassicExtensionId || !version || !package_hash ||
      package_hash->size() != 64u || !crx_key_hash ||
      crx_key_hash->size() != 64u || !update_manifest_url) {
    return std::nullopt;
  }
  state.extension_id = *extension_id;
  state.version = base::Version(*version);
  state.package_sha256 = *package_hash;
  state.crx_public_key_sha256 = *crx_key_hash;
  state.update_manifest_url = GURL(*update_manifest_url);
  const bool valid_signed_update_url =
      state.update_manifest_url.is_valid() &&
      state.update_manifest_url.SchemeIs("https") &&
      !state.update_manifest_url.has_username() &&
      !state.update_manifest_url.has_password() &&
      !state.update_manifest_url.has_ref();
  const bool exact_pinned_bootstrap =
      state.sequence == kUboClassicBootstrapSequence &&
      state.update_manifest_url.is_empty() &&
      IsPinnedUboBootstrapIdentity(
          state.extension_id, state.version.GetString(), state.package_sha256,
          state.crx_public_key_sha256);
  if (!state.version.IsValid() ||
      (!valid_signed_update_url && !exact_pinned_bootstrap)) {
    return std::nullopt;
  }
  return state;
}

bool IsRollback(const UboAuthorizationState& candidate,
                const UboAuthorizationState& committed) {
  if (candidate.sequence < committed.sequence ||
      candidate.version < committed.version) {
    return true;
  }
  if (candidate.sequence == committed.sequence) {
    return !StateEquals(candidate, committed);
  }
  return candidate.version == committed.version &&
         candidate.package_sha256 != committed.package_sha256;
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  // Intentionally not SYNCABLE_PREF. Extension authorization and storage are
  // local security state and never cross Ahoi CloudKit or Chrome Sync.
  registry->RegisterDictionaryPref(kUboAuthorizationPref);
}

std::optional<UboAuthorizationState> ReadCommittedUboAuthorization(
    const PrefService& prefs) {
  if (!prefs.FindPreference(kUboAuthorizationPref)) {
    return std::nullopt;
  }
  return ParseState(prefs.GetDict(kUboAuthorizationPref));
}

base::expected<void, UboVerificationError>
CheckUboCatalogAgainstCommittedAuthorization(const PrefService& prefs,
                                             const UboCatalogEntry& entry) {
  UboAuthorizationState candidate{
      .sequence = entry.sequence,
      .extension_id = entry.extension_id,
      .version = entry.version,
      .package_sha256 = entry.package_sha256,
      .crx_public_key_sha256 = entry.crx_public_key_sha256,
      .update_manifest_url = entry.update_manifest_url,
  };
  if (std::optional<UboAuthorizationState> committed =
          ReadCommittedUboAuthorization(prefs);
      committed && IsRollback(candidate, *committed)) {
    return base::unexpected(UboVerificationError::kRollback);
  }
  return base::ok();
}

void ClearCommittedUboAuthorization(PrefService* prefs) {
  if (!prefs) {
    return;
  }
  PendingRegistry& registry = GetPendingRegistry();
  {
    base::AutoLock lock(registry.lock);
    registry.entries.erase(prefs);
  }
  if (prefs->FindPreference(kUboAuthorizationPref) &&
      !prefs->IsManagedPreference(kUboAuthorizationPref)) {
    prefs->ClearPref(kUboAuthorizationPref);
  }
}

bool IsUboManifestV2ExtensionAllowed(const PrefService& prefs,
                                     const ::extensions::Extension& extension) {
  if (!IsUboClassicEnabled() || extension.manifest_version() != 2 ||
      extension.id() != kUboClassicExtensionId) {
    return false;
  }

  PendingRegistry& registry = GetPendingRegistry();
  {
    base::AutoLock lock(registry.lock);
    auto it = registry.entries.find(&prefs);
    if (it != registry.entries.end() &&
        ExtensionMatchesState(extension, it->second.state)) {
      return true;
    }
  }

  std::optional<UboAuthorizationState> committed =
      ReadCommittedUboAuthorization(prefs);
  return committed && ExtensionMatchesState(extension, *committed);
}

UboInstallAuthorization::UboInstallAuthorization(PrefService* prefs,
                                                 UboAuthorizationState state,
                                                 uint64_t token)
    : prefs_(prefs), state_(std::move(state)), token_(token) {}

UboInstallAuthorization::~UboInstallAuthorization() {
  Abort();
}

base::expected<void, UboVerificationError> UboInstallAuthorization::Commit(
    const ::extensions::Extension& installed_extension) {
  if (!active_ || !prefs_ ||
      !ExtensionMatchesState(installed_extension, state_)) {
    return base::unexpected(UboVerificationError::kInstalledExtensionMismatch);
  }

  PendingRegistry& registry = GetPendingRegistry();
  {
    base::AutoLock lock(registry.lock);
    auto it = registry.entries.find(prefs_);
    if (it == registry.entries.end() || it->second.token != token_ ||
        !StateEquals(it->second.state, state_)) {
      return base::unexpected(UboVerificationError::kAuthorizationConflict);
    }
  }

  if (!prefs_->FindPreference(kUboAuthorizationPref) ||
      prefs_->IsManagedPreference(kUboAuthorizationPref)) {
    return base::unexpected(UboVerificationError::kStateWriteFailed);
  }
  base::DictValue previous_state =
      prefs_->GetDict(kUboAuthorizationPref).Clone();
  prefs_->SetDict(kUboAuthorizationPref, SerializeState(state_));
  std::optional<UboAuthorizationState> readback =
      ReadCommittedUboAuthorization(*prefs_);
  if (!readback || !StateEquals(*readback, state_)) {
    // Do not leave a partially written or mismatched authorization behind.
    // The pending in-memory exception remains active only until the caller
    // aborts this transaction and removes a newly installed package.
    prefs_->SetDict(kUboAuthorizationPref, std::move(previous_state));
    return base::unexpected(UboVerificationError::kStateWriteFailed);
  }

  Abort();
  return base::ok();
}

void UboInstallAuthorization::Abort() {
  if (!active_ || !prefs_) {
    return;
  }
  PendingRegistry& registry = GetPendingRegistry();
  base::AutoLock lock(registry.lock);
  auto it = registry.entries.find(prefs_);
  if (it != registry.entries.end() && it->second.token == token_) {
    registry.entries.erase(it);
  }
  active_ = false;
}

base::expected<std::unique_ptr<UboInstallAuthorization>, UboVerificationError>
BeginUboInstallAuthorization(PrefService* prefs,
                             const UboCatalogEntry& entry,
                             const VerifiedUboPackage& package) {
  if (!prefs || !prefs->FindPreference(kUboAuthorizationPref) ||
      prefs->IsManagedPreference(kUboAuthorizationPref) ||
      entry.extension_id != kUboClassicExtensionId ||
      !HasAllowedUpdateContract(entry) ||
      package.extension_id != entry.extension_id ||
      package.package_sha256 != entry.package_sha256 ||
      package.crx_public_key_sha256 != entry.crx_public_key_sha256) {
    return base::unexpected(UboVerificationError::kInstalledExtensionMismatch);
  }

  if (auto rollback =
          CheckUboCatalogAgainstCommittedAuthorization(*prefs, entry);
      !rollback.has_value()) {
    return base::unexpected(rollback.error());
  }

  UboAuthorizationState candidate{
      .sequence = entry.sequence,
      .extension_id = entry.extension_id,
      .version = entry.version,
      .package_sha256 = entry.package_sha256,
      .crx_public_key_sha256 = entry.crx_public_key_sha256,
      .update_manifest_url = entry.update_manifest_url,
  };

  PendingRegistry& registry = GetPendingRegistry();
  base::AutoLock lock(registry.lock);
  if (registry.entries.contains(prefs)) {
    return base::unexpected(UboVerificationError::kAuthorizationConflict);
  }
  const uint64_t token = registry.next_token++;
  registry.entries.emplace(prefs, PendingAuthorization{token, candidate});
  return std::unique_ptr<UboInstallAuthorization>(
      new UboInstallAuthorization(prefs, std::move(candidate), token));
}

}  // namespace ahoi::extensions
