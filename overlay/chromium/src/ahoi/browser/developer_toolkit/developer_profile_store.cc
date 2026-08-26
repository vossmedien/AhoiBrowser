// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"

#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_codec.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

constexpr char kVersion[] = "version";
constexpr char kOrigins[] = "origins";

bool IsSupportedOrigin(const url::Origin& origin) {
  const GURL url = origin.GetURL();
  return !origin.opaque() && url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

const base::DictValue* GetOrigins(const base::DictValue& root) {
  const std::optional<int> version = root.FindInt(kVersion);
  if (!version ||
      (*version != 1 && *version != kDeveloperProfileSchemaVersion)) {
    return nullptr;
  }
  return root.FindDict(kOrigins);
}

base::DictValue* GetMutableOrigins(base::DictValue& root) {
  const base::Value* origins_value = root.Find(kOrigins);
  const bool has_origins = origins_value != nullptr;
  if (origins_value && !origins_value->is_dict()) {
    return nullptr;
  }

  const base::Value* version_value = root.Find(kVersion);
  if (!version_value) {
    root.Set(kVersion, kDeveloperProfileSchemaVersion);
  } else {
    const std::optional<int> version = version_value->GetIfInt();
    if (!version ||
        (*version != 1 && *version != kDeveloperProfileSchemaVersion)) {
      return nullptr;
    }
    if (*version == 1) {
      root.Set(kVersion, kDeveloperProfileSchemaVersion);
    }
  }

  if (!has_origins) {
    root.Set(kOrigins, base::DictValue());
  } else if (!origins_value->is_dict()) {
    return nullptr;
  }
  return root.FindDict(kOrigins);
}

std::optional<url::Origin> ParseStoredOrigin(std::string_view key) {
  const GURL url{std::string(key)};
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  url::Origin origin = url::Origin::Create(url);
  if (origin.opaque() || origin.Serialize() != key) {
    return std::nullopt;
  }
  return origin;
}

std::optional<DeveloperProfile> DecodeValidProfile(
    const url::Origin& origin,
    const base::DictValue& value) {
  std::optional<DeveloperProfile> profile =
      DeserializeDeveloperProfile(value, &origin);
  if (!profile || ValidateDeveloperProfile(origin, *profile) !=
                      DeveloperProfileValidationError::kNone) {
    return std::nullopt;
  }
  return profile;
}

}  // namespace

PrefDeveloperProfileStore::PrefDeveloperProfileStore(
    PrefService* prefs,
    bool is_off_the_record,
    bool allow_incognito_overrides)
    : prefs_(prefs),
      is_off_the_record_(is_off_the_record),
      allow_incognito_overrides_(allow_incognito_overrides) {}

PrefDeveloperProfileStore::~PrefDeveloperProfileStore() = default;

bool PrefDeveloperProfileStore::CanAccess() const {
  return prefs_ && (!is_off_the_record_ || allow_incognito_overrides_);
}

std::optional<DeveloperProfile> PrefDeveloperProfileStore::Get(
    const url::Origin& origin) const {
  if (!CanAccess() || !IsSupportedOrigin(origin)) {
    return std::nullopt;
  }
  const base::DictValue* origins =
      GetOrigins(prefs_->GetDict(kDeveloperProfilesPref));
  if (!origins) {
    return std::nullopt;
  }
  const base::DictValue* value = origins->FindDict(origin.Serialize());
  return value ? DecodeValidProfile(origin, *value) : std::nullopt;
}

bool PrefDeveloperProfileStore::Set(const url::Origin& origin,
                                    const DeveloperProfile& profile) {
  if (!CanAccess() || ValidateDeveloperProfileForPersistence(origin, profile) !=
                          DeveloperProfileValidationError::kNone) {
    return false;
  }
  std::optional<base::DictValue> encoded = SerializeDeveloperProfile(profile);
  if (!encoded) {
    return false;
  }

  ScopedDictPrefUpdate update(prefs_, kDeveloperProfilesPref);
  base::DictValue& root = update.Get();
  base::DictValue* origins = GetMutableOrigins(root);
  if (!origins) {
    return false;
  }
  const std::string key = origin.Serialize();
  if (!origins->Find(key) && origins->size() >= kMaxDeveloperProfiles) {
    return false;
  }
  origins->Set(key, std::move(*encoded));
  return true;
}

bool PrefDeveloperProfileStore::Remove(const url::Origin& origin) {
  if (!CanAccess() || !IsSupportedOrigin(origin)) {
    return false;
  }
  const base::DictValue& root = prefs_->GetDict(kDeveloperProfilesPref);
  const base::DictValue* origins = GetOrigins(root);
  if (!origins || !origins->Find(origin.Serialize())) {
    return false;
  }
  ScopedDictPrefUpdate update(prefs_, kDeveloperProfilesPref);
  base::DictValue* mutable_origins = GetMutableOrigins(update.Get());
  return mutable_origins && mutable_origins->Remove(origin.Serialize());
}

std::vector<url::Origin> PrefDeveloperProfileStore::ListOrigins() const {
  if (!CanAccess()) {
    return {};
  }
  const base::DictValue* origins =
      GetOrigins(prefs_->GetDict(kDeveloperProfilesPref));
  if (!origins) {
    return {};
  }
  std::vector<url::Origin> result;
  for (const auto [key, value] : *origins) {
    std::optional<url::Origin> origin = ParseStoredOrigin(key);
    const base::DictValue* profile = value.GetIfDict();
    if (origin && profile && DecodeValidProfile(*origin, *profile)) {
      result.push_back(*origin);
    }
  }
  return result;
}

InMemoryDeveloperProfileStore::InMemoryDeveloperProfileStore(
    bool is_off_the_record,
    bool allow_incognito_overrides)
    : is_off_the_record_(is_off_the_record),
      allow_incognito_overrides_(allow_incognito_overrides) {}

InMemoryDeveloperProfileStore::~InMemoryDeveloperProfileStore() = default;

bool InMemoryDeveloperProfileStore::CanAccess() const {
  return !is_off_the_record_ || allow_incognito_overrides_;
}

std::optional<DeveloperProfile> InMemoryDeveloperProfileStore::Get(
    const url::Origin& origin) const {
  if (!CanAccess() || !IsSupportedOrigin(origin)) {
    return std::nullopt;
  }
  auto it = profiles_.find(origin.Serialize());
  return it == profiles_.end()
             ? std::nullopt
             : std::optional<DeveloperProfile>(it->second.second);
}

bool InMemoryDeveloperProfileStore::Set(const url::Origin& origin,
                                        const DeveloperProfile& profile) {
  if (!CanAccess() || ValidateDeveloperProfile(origin, profile) !=
                          DeveloperProfileValidationError::kNone) {
    return false;
  }
  const std::string key = origin.Serialize();
  if (!profiles_.contains(key) && profiles_.size() >= kMaxDeveloperProfiles) {
    return false;
  }
  profiles_.insert_or_assign(key, std::make_pair(origin, profile));
  return true;
}

bool InMemoryDeveloperProfileStore::Remove(const url::Origin& origin) {
  if (!CanAccess() || !IsSupportedOrigin(origin)) {
    return false;
  }
  return profiles_.erase(origin.Serialize()) != 0;
}

std::vector<url::Origin> InMemoryDeveloperProfileStore::ListOrigins() const {
  if (!CanAccess()) {
    return {};
  }
  std::vector<url::Origin> result;
  result.reserve(profiles_.size());
  for (const auto& [key, entry] : profiles_) {
    result.push_back(entry.first);
  }
  return result;
}

}  // namespace ahoi
