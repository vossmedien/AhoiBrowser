// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "ahoi/browser/http_auth/http_auth_credential_service_internal.h"
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

namespace ahoi {

bool HttpAuthCredentialService::SetPreferredCredential(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view username,
    HttpAuthRequestContext request_context) {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid || username.empty() ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_) {
    return false;
  }
  bool found = false;
  for (HttpAuthCredentialMetadata& metadata : state.credentials) {
    if (!http_auth_internal::IsSameRealmAndNetwork(metadata.protection_space,
                                                   protection_space)) {
      continue;
    }
    if (metadata.username == username) {
      metadata.preferred = true;
      found = true;
    } else {
      metadata.preferred = false;
    }
  }
  return found && http_auth_internal::WriteMetadataState(prefs_, state);
}

bool HttpAuthCredentialService::SetNeverSaveForRealm(
    const HttpAuthProtectionSpace& protection_space,
    bool never_save,
    HttpAuthRequestContext request_context) {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid || !protection_space.IsValid() ||
      request_context == HttpAuthRequestContext::kIncognito ||
      profile_is_incognito_for_testing_) {
    return false;
  }
  std::erase_if(state.never_save,
                [&protection_space](const HttpAuthProtectionSpace& existing) {
                  return existing.MatchesRealm(protection_space);
                });
  if (never_save) {
    HttpAuthProtectionSpace realm = protection_space;
    realm.permitted_paths.clear();
    // Never-save is deliberately realm-wide, not partition-wide. Store an
    // empty, persistable NAK sentinel so a transient request key cannot make
    // the user's suppression choice disappear.
    realm.network_anonymization_key = {};
    realm.network_anonymization_key_metadata_value.reset();
    state.never_save.push_back(std::move(realm));
  }
  return http_auth_internal::WriteMetadataState(prefs_, state);
}

bool HttpAuthCredentialService::IsNeverSaveForRealm(
    const HttpAuthProtectionSpace& protection_space) const {
  const http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid) {
    return true;
  }
  return std::ranges::any_of(
      state.never_save,
      [&protection_space](const HttpAuthProtectionSpace& existing) {
        return existing.MatchesRealm(protection_space);
      });
}

std::vector<HttpAuthCredentialMetadata>
HttpAuthCredentialService::GetMetadataSnapshot() const {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  return state.valid ? std::move(state.credentials)
                     : std::vector<HttpAuthCredentialMetadata>();
}

std::vector<HttpAuthProtectionSpace>
HttpAuthCredentialService::GetNeverSaveSnapshot() const {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  return state.valid ? std::move(state.never_save)
                     : std::vector<HttpAuthProtectionSpace>();
}

std::optional<HttpAuthCredentialMetadata>
HttpAuthCredentialService::FindMetadata(
    const HttpAuthProtectionSpace& protection_space,
    std::string_view request_path,
    std::u16string_view username) const {
  const http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid) {
    return std::nullopt;
  }
  for (const HttpAuthCredentialMetadata& metadata : state.credentials) {
    if (metadata.username == username &&
        metadata.protection_space.Matches(protection_space, request_path)) {
      return metadata;
    }
  }
  return std::nullopt;
}

bool HttpAuthCredentialService::PersistMetadata(
    const std::vector<HttpAuthCredentialMetadata>& metadata) const {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid) {
    return false;
  }
  state.credentials = metadata;
  return http_auth_internal::WriteMetadataState(prefs_, state);
}

void HttpAuthCredentialService::RecordMetadataAfterSuccess(
    const HttpAuthProtectionSpace& protection_space,
    std::string_view request_path,
    std::u16string_view username,
    base::Time success_time) {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid || !protection_space.IsValid()) {
    return;
  }

  HttpAuthCredentialMetadata* matching = nullptr;
  for (HttpAuthCredentialMetadata& metadata : state.credentials) {
    if (metadata.username == username &&
        http_auth_internal::IsSameRealmAndNetwork(metadata.protection_space,
                                                  protection_space)) {
      matching = &metadata;
      break;
    }
  }

  if (!matching) {
    HttpAuthCredentialMetadata metadata;
    metadata.protection_space = protection_space;
    metadata.protection_space.AddPermittedPath(request_path);
    metadata.username = std::u16string(username);
    metadata.last_successful = success_time;
    const bool has_existing_preferred = std::ranges::any_of(
        state.credentials,
        [&protection_space](const HttpAuthCredentialMetadata& existing) {
          return http_auth_internal::IsSameRealmAndNetwork(
                     existing.protection_space, protection_space) &&
                 existing.preferred;
        });
    metadata.preferred = !has_existing_preferred;
    state.credentials.push_back(std::move(metadata));
  } else {
    matching->protection_space.AddPermittedPath(request_path);
    matching->last_successful = success_time;
  }
  http_auth_internal::WriteMetadataState(prefs_, state);
}

void HttpAuthCredentialService::DeleteMetadata(
    const HttpAuthProtectionSpace& protection_space,
    std::optional<std::u16string_view> username) {
  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid) {
    return;
  }
  std::erase_if(state.credentials, [&protection_space, username](
                                       const HttpAuthCredentialMetadata& item) {
    // PasswordStore has no NetworkAnonymizationKey dimension. Removing its
    // exact origin/scheme/realm/username secret must also remove every
    // partitioned metadata reference to that secret.
    if (!item.protection_space.MatchesRealm(protection_space)) {
      return false;
    }
    return !username || item.username == *username;
  });

  // Keep one predictable default in every remaining network partition after
  // deleting a preferred row. This mutates metadata only; no secret is read.
  for (HttpAuthCredentialMetadata& candidate : state.credentials) {
    if (!candidate.protection_space.MatchesRealm(protection_space)) {
      continue;
    }
    const bool has_preferred = std::ranges::any_of(
        state.credentials,
        [&candidate](const HttpAuthCredentialMetadata& item) {
          return http_auth_internal::IsSameRealmAndNetwork(
                     item.protection_space, candidate.protection_space) &&
                 item.preferred;
        });
    if (has_preferred) {
      continue;
    }
    HttpAuthCredentialMetadata* replacement = nullptr;
    for (HttpAuthCredentialMetadata& item : state.credentials) {
      if (http_auth_internal::IsSameRealmAndNetwork(
              item.protection_space, candidate.protection_space) &&
          (!replacement ||
           item.last_successful > replacement->last_successful)) {
        replacement = &item;
      }
    }
    if (replacement) {
      replacement->preferred = true;
    }
  }
  http_auth_internal::WriteMetadataState(prefs_, state);
}

bool HttpAuthCredentialService::CanRenameCredentialMetadata(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view old_username,
    std::u16string_view new_username) const {
  const http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid || !protection_space.IsValid() || old_username.empty() ||
      new_username.empty()) {
    return false;
  }

  bool found_old_username = false;
  for (const HttpAuthCredentialMetadata& metadata : state.credentials) {
    if (!metadata.protection_space.MatchesRealm(protection_space)) {
      continue;
    }
    found_old_username |= metadata.username == old_username;
    if (old_username != new_username && metadata.username == new_username) {
      return false;
    }
  }
  return found_old_username;
}

bool HttpAuthCredentialService::RenameCredentialMetadata(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view old_username,
    std::u16string_view new_username) {
  if (!CanRenameCredentialMetadata(protection_space, old_username,
                                   new_username)) {
    return false;
  }
  if (old_username == new_username) {
    return true;
  }

  http_auth_internal::MetadataState state =
      http_auth_internal::ReadMetadataState(prefs_);
  if (!state.valid) {
    return false;
  }
  for (HttpAuthCredentialMetadata& metadata : state.credentials) {
    // PasswordStore has no NAK dimension, so rename every partitioned
    // metadata reference to the same realm/username secret.
    if (metadata.protection_space.MatchesRealm(protection_space) &&
        metadata.username == old_username) {
      metadata.username = std::u16string(new_username);
    }
  }
  return http_auth_internal::WriteMetadataState(prefs_, state);
}

}  // namespace ahoi
