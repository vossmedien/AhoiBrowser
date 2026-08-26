// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_management_model.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_credential_service_internal.h"
#include "base/i18n/string_search.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_auth.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi {
namespace {

std::u16string SearchText(const HttpAuthCredentialMetadata& metadata) {
  const HttpAuthProtectionSpace& space = metadata.protection_space;
  const std::string_view scheme =
      space.scheme == net::HttpAuth::AUTH_SCHEME_BASIC ? "Basic" : "Digest";
  const std::string_view target =
      space.target == net::HttpAuth::AUTH_PROXY ? "proxy" : "server";
  const std::string origin = base::StrCat(
      {space.origin.scheme(), "://",
       net::HostPortPair(space.origin.host(), space.origin.port()).ToString()});
  return base::StrCat({base::UTF8ToUTF16(origin), u" ",
                       base::UTF8ToUTF16(space.realm), u" ", metadata.username,
                       u" ", base::UTF8ToUTF16(scheme), u" ",
                       base::UTF8ToUTF16(target)});
}

bool MatchesQuery(const HttpAuthCredentialMetadata& metadata,
                  std::u16string_view query) {
  if (query.empty()) {
    return true;
  }
  return base::i18n::StringSearchIgnoringCaseAndAccents(
      std::u16string(query), SearchText(metadata), nullptr, nullptr);
}

bool SortBefore(const HttpAuthCredentialMetadata& lhs,
                const HttpAuthCredentialMetadata& rhs) {
  const auto lhs_group = std::tuple(
      lhs.protection_space.origin.Serialize(), lhs.protection_space.realm,
      lhs.protection_space.target, lhs.protection_space.scheme);
  const auto rhs_group = std::tuple(
      rhs.protection_space.origin.Serialize(), rhs.protection_space.realm,
      rhs.protection_space.target, rhs.protection_space.scheme);
  if (lhs_group != rhs_group) {
    return lhs_group < rhs_group;
  }
  if (lhs.preferred != rhs.preferred) {
    return lhs.preferred;
  }
  if (lhs.last_successful != rhs.last_successful) {
    return lhs.last_successful > rhs.last_successful;
  }
  return lhs.username < rhs.username;
}

}  // namespace

std::vector<HttpAuthManagementEntry> BuildHttpAuthManagementEntries(
    std::vector<HttpAuthCredentialMetadata> metadata,
    std::u16string_view query,
    const GURL& source_url,
    const std::optional<HttpAuthProtectionSpace>& active_protection_space) {
  std::ranges::sort(metadata, &SortBefore);
  std::vector<HttpAuthManagementEntry> entries;
  entries.reserve(metadata.size());
  for (HttpAuthCredentialMetadata& item : metadata) {
    if (!item.protection_space.IsValid() || item.username.empty() ||
        !MatchesQuery(item, query)) {
      continue;
    }
    const bool active_realm =
        active_protection_space &&
        http_auth_internal::IsSameRealmAndNetwork(item.protection_space,
                                                  *active_protection_space);
    const bool can_switch = CanSwitchManagedHttpAuthAccount(
        item, source_url, active_protection_space);
    auto existing = std::ranges::find_if(
        entries, [&item](const HttpAuthManagementEntry& entry) {
          return IsSameManagedHttpAuthCredential(entry.metadata, item);
        });
    if (existing == entries.end()) {
      entries.push_back({
          .metadata = std::move(item),
          .is_active_realm = active_realm,
          .can_switch_account = can_switch,
      });
      continue;
    }

    // One PasswordStore secret can have multiple metadata rows because cache
    // reuse remains partitioned by NAK. Present one account row. For an active
    // realm, retain the exact active-partition metadata so switch/preference
    // actions cannot target a sibling partition.
    const bool any_preferred = existing->metadata.preferred || item.preferred;
    const base::Time latest_success =
        std::max(existing->metadata.last_successful, item.last_successful);
    if (can_switch && !existing->can_switch_account) {
      existing->metadata = std::move(item);
    }
    existing->is_active_realm = existing->is_active_realm || active_realm;
    existing->can_switch_account = existing->can_switch_account || can_switch;
    if (!existing->can_switch_account) {
      existing->metadata.preferred = any_preferred;
    }
    existing->metadata.last_successful = latest_success;
  }
  std::ranges::sort(entries, [](const HttpAuthManagementEntry& lhs,
                                const HttpAuthManagementEntry& rhs) {
    return SortBefore(lhs.metadata, rhs.metadata);
  });
  return entries;
}

bool HttpAuthProtectionSpaceMatchesManagementQuery(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string_view query) {
  if (query.empty()) {
    return true;
  }
  HttpAuthCredentialMetadata metadata;
  metadata.protection_space = protection_space;
  return MatchesQuery(metadata, query);
}

bool CanSwitchManagedHttpAuthAccount(
    const HttpAuthCredentialMetadata& metadata,
    const GURL& source_url,
    const std::optional<HttpAuthProtectionSpace>& active_protection_space) {
  if (!source_url.SchemeIsHTTPOrHTTPS() || !active_protection_space ||
      !metadata.protection_space.IsValid() ||
      url::Origin::Create(source_url) !=
          url::Origin::Create(metadata.protection_space.OriginUrl())) {
    return false;
  }
  return http_auth_internal::IsSameRealmAndNetwork(metadata.protection_space,
                                                   *active_protection_space);
}

bool IsSameManagedHttpAuthCredential(const HttpAuthCredentialMetadata& lhs,
                                     const HttpAuthCredentialMetadata& rhs) {
  return lhs.username == rhs.username &&
         IsSameManagedHttpAuthRealm(lhs.protection_space, rhs.protection_space);
}

bool IsSameManagedHttpAuthRealm(const HttpAuthProtectionSpace& lhs,
                                const HttpAuthProtectionSpace& rhs) {
  return lhs.MatchesRealm(rhs);
}

}  // namespace ahoi
