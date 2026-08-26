// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_credential_service_internal.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "ahoi/browser/http_auth/http_auth_prefs.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_auth_scheme.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace ahoi::http_auth_internal {

constexpr char kVersionKey[] = "version";
constexpr char kCredentialsKey[] = "credentials";
constexpr char kNeverSaveKey[] = "never_save";
constexpr char kTargetKey[] = "target";
constexpr char kOriginKey[] = "origin";
constexpr char kSchemeKey[] = "scheme";
constexpr char kRealmKey[] = "realm";
constexpr char kPathsKey[] = "paths";
constexpr char kNetworkAnonymizationKey[] = "network_anonymization_key";
constexpr char kUsernameKey[] = "username";
constexpr char kPreferredKey[] = "preferred";
constexpr char kLastSuccessfulKey[] = "last_successful";

bool IsSupportedScheme(net::HttpAuth::Scheme scheme) {
  return scheme == net::HttpAuth::AUTH_SCHEME_BASIC ||
         scheme == net::HttpAuth::AUTH_SCHEME_DIGEST;
}

bool IsSupportedTarget(net::HttpAuth::Target target) {
  return target == net::HttpAuth::AUTH_SERVER ||
         target == net::HttpAuth::AUTH_PROXY;
}

bool IsHttpOrHttps(const url::SchemeHostPort& origin) {
  return origin.scheme() == url::kHttpScheme ||
         origin.scheme() == url::kHttpsScheme;
}

password_manager::PasswordForm::Scheme ToPasswordFormScheme(
    net::HttpAuth::Scheme scheme) {
  return scheme == net::HttpAuth::AUTH_SCHEME_BASIC
             ? password_manager::PasswordForm::Scheme::kBasic
             : password_manager::PasswordForm::Scheme::kDigest;
}

std::optional<base::Value> NetworkAnonymizationKeyValueForMatching(
    const HttpAuthProtectionSpace& protection_space) {
  base::Value value;
  if (protection_space.network_anonymization_key_metadata_value) {
    value = protection_space.network_anonymization_key_metadata_value->Clone();
  } else if (!protection_space.network_anonymization_key.ToValue(&value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<base::DictValue> SerializeProtectionSpace(
    const HttpAuthProtectionSpace& protection_space) {
  base::Value network_anonymization_key;
  if (protection_space.network_anonymization_key_metadata_value) {
    network_anonymization_key =
        protection_space.network_anonymization_key_metadata_value->Clone();
  } else if (!protection_space.network_anonymization_key.ToValue(
                 &network_anonymization_key)) {
    // A nonce- or opaque-origin NAK is intentionally session-only. Refuse to
    // persist a credential metadata row rather than weaken the partition key.
    return std::nullopt;
  }

  base::DictValue value;
  value.Set(kTargetKey, static_cast<int>(protection_space.target));
  value.Set(kOriginKey, protection_space.origin.Serialize());
  value.Set(kSchemeKey, static_cast<int>(protection_space.scheme));
  value.Set(kRealmKey, protection_space.realm);
  value.Set(kNetworkAnonymizationKey, std::move(network_anonymization_key));

  base::ListValue paths;
  for (const std::string& path : protection_space.permitted_paths) {
    paths.Append(path);
  }
  value.Set(kPathsKey, std::move(paths));
  return std::optional<base::DictValue>(std::move(value));
}

std::optional<HttpAuthProtectionSpace> DeserializeProtectionSpace(
    const base::DictValue& value) {
  const std::optional<int> target = value.FindInt(kTargetKey);
  const std::string* origin_string = value.FindString(kOriginKey);
  const std::optional<int> scheme = value.FindInt(kSchemeKey);
  const std::string* realm = value.FindString(kRealmKey);
  if (!target || !origin_string || !scheme || !realm) {
    return std::nullopt;
  }

  if (*target < net::HttpAuth::AUTH_PROXY ||
      *target >= net::HttpAuth::AUTH_NUM_TARGETS ||
      *scheme < net::HttpAuth::AUTH_SCHEME_BASIC ||
      *scheme >= net::HttpAuth::AUTH_SCHEME_MAX) {
    return std::nullopt;
  }

  const GURL origin_url(*origin_string);
  url::SchemeHostPort origin(origin_url);
  HttpAuthProtectionSpace result(
      static_cast<net::HttpAuth::Target>(*target), std::move(origin),
      static_cast<net::HttpAuth::Scheme>(*scheme), *realm);
  if (!result.IsValid()) {
    return std::nullopt;
  }

  const base::Value* network_anonymization_key =
      value.Find(kNetworkAnonymizationKey);
  if (!network_anonymization_key ||
      !net::NetworkAnonymizationKey::FromValue(
          *network_anonymization_key, &result.network_anonymization_key)) {
    return std::nullopt;
  }
  result.network_anonymization_key_metadata_value =
      network_anonymization_key->Clone();

  const base::ListValue* paths = value.FindList(kPathsKey);
  if (paths) {
    for (const base::Value& path : *paths) {
      const std::string* path_string = path.GetIfString();
      if (!path_string) {
        return std::nullopt;
      }
      result.AddPermittedPath(*path_string);
    }
  }
  return result;
}

MetadataState ReadMetadataState(const PrefService* prefs) {
  MetadataState state;
  if (!prefs) {
    state.valid = false;
    return state;
  }

  const base::DictValue& root =
      prefs->GetDict(http_auth_prefs::kCredentialMetadata);
  const std::optional<int> version = root.FindInt(kVersionKey);
  if (!version) {
    // A freshly registered dictionary is empty and represents version one.
    return state;
  }
  if (*version != HttpAuthCredentialService::kCurrentMetadataVersion) {
    state.valid = false;
    return state;
  }

  if (const base::ListValue* credentials = root.FindList(kCredentialsKey)) {
    for (const base::Value& item : *credentials) {
      const base::DictValue* dict = item.GetIfDict();
      if (!dict) {
        state.valid = false;
        return state;
      }
      const std::string* username = dict->FindString(kUsernameKey);
      if (!username) {
        state.valid = false;
        return state;
      }
      std::optional<HttpAuthProtectionSpace> protection_space =
          DeserializeProtectionSpace(*dict);
      if (!protection_space) {
        state.valid = false;
        return state;
      }
      HttpAuthCredentialMetadata metadata;
      metadata.protection_space = std::move(*protection_space);
      metadata.username = base::UTF8ToUTF16(*username);
      metadata.preferred = dict->FindBool(kPreferredKey).value_or(false);
      if (const base::Value* last_successful = dict->Find(kLastSuccessfulKey)) {
        metadata.last_successful =
            base::ValueToTime(last_successful).value_or(base::Time());
      }
      state.credentials.push_back(std::move(metadata));
    }
  }

  if (const base::ListValue* never_save = root.FindList(kNeverSaveKey)) {
    for (const base::Value& item : *never_save) {
      const base::DictValue* dict = item.GetIfDict();
      if (!dict) {
        state.valid = false;
        return state;
      }
      std::optional<HttpAuthProtectionSpace> protection_space =
          DeserializeProtectionSpace(*dict);
      if (!protection_space) {
        state.valid = false;
        return state;
      }
      state.never_save.push_back(std::move(*protection_space));
    }
  }
  return state;
}

bool WriteMetadataState(PrefService* prefs, const MetadataState& state) {
  if (!prefs || !state.valid) {
    return false;
  }

  base::DictValue root;
  root.Set(kVersionKey, HttpAuthCredentialService::kCurrentMetadataVersion);

  base::ListValue credentials;
  for (const HttpAuthCredentialMetadata& metadata : state.credentials) {
    std::optional<base::DictValue> item =
        SerializeProtectionSpace(metadata.protection_space);
    if (!item) {
      return false;
    }
    item->Set(kUsernameKey, base::UTF16ToUTF8(metadata.username));
    item->Set(kPreferredKey, metadata.preferred);
    if (!metadata.last_successful.is_null()) {
      item->Set(kLastSuccessfulKey,
                base::TimeToValue(metadata.last_successful));
    }
    credentials.Append(std::move(*item));
  }
  root.Set(kCredentialsKey, std::move(credentials));

  base::ListValue never_save;
  for (const HttpAuthProtectionSpace& protection_space : state.never_save) {
    std::optional<base::DictValue> item =
        SerializeProtectionSpace(protection_space);
    if (!item) {
      return false;
    }
    never_save.Append(std::move(*item));
  }
  root.Set(kNeverSaveKey, std::move(never_save));
  prefs->SetDict(http_auth_prefs::kCredentialMetadata, std::move(root));
  return true;
}

bool IsSameNetworkAnonymizationKey(const HttpAuthProtectionSpace& lhs,
                                   const HttpAuthProtectionSpace& rhs) {
  const std::optional<base::Value> lhs_value =
      NetworkAnonymizationKeyValueForMatching(lhs);
  const std::optional<base::Value> rhs_value =
      NetworkAnonymizationKeyValueForMatching(rhs);
  if (lhs_value && rhs_value) {
    return *lhs_value == *rhs_value;
  }
  // Non-persistable keys may still match within one live session, but can
  // never match a metadata-loaded value or be written to disk.
  return !lhs.network_anonymization_key_metadata_value &&
         !rhs.network_anonymization_key_metadata_value &&
         lhs.network_anonymization_key == rhs.network_anonymization_key;
}

bool IsSameRealmAndNetwork(const HttpAuthProtectionSpace& lhs,
                           const HttpAuthProtectionSpace& rhs) {
  return lhs.MatchesRealm(rhs) && IsSameNetworkAnonymizationKey(lhs, rhs);
}

bool IsCredentialNewer(const HttpAuthCredential& lhs,
                       const HttpAuthCredential& rhs) {
  if (lhs.metadata.preferred != rhs.metadata.preferred) {
    return lhs.metadata.preferred;
  }
  if (lhs.metadata.last_successful != rhs.metadata.last_successful) {
    return lhs.metadata.last_successful > rhs.metadata.last_successful;
  }
  return lhs.metadata.username < rhs.metadata.username;
}

}  // namespace ahoi::http_auth_internal
