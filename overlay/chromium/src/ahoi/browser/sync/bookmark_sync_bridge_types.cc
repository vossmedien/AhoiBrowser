// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"

#include <algorithm>
#include <array>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"

namespace ahoi::sync {

bool ParseNativeBookmarkKey(const std::string& key,
                            base::Uuid* uuid,
                            bool* account) {
  if (!uuid || !account) {
    return false;
  }
  const bool is_account = key.starts_with("account:");
  const bool is_local = key.starts_with("local:");
  if (!is_account && !is_local) {
    return false;
  }
  const base::Uuid parsed =
      base::Uuid::ParseLowercase(key.substr(is_account ? 8u : 6u));
  if (!parsed.is_valid()) {
    return false;
  }
  *uuid = parsed;
  *account = is_account;
  return true;
}

std::string NativeBookmarkKey(const base::Uuid& uuid, bool account) {
  return (account ? "account:" : "local:") + uuid.AsLowercaseString();
}

base::Uuid InitialBookmarkSyncId(const std::string& native_key) {
  base::Uuid uuid;
  bool account = false;
  if (!ParseNativeBookmarkKey(native_key, &uuid, &account)) {
    return {};
  }
  // Deterministic first binding closes the crash window between the first
  // native save and the journal commit. Native GUIDs already have random
  // entropy; this domain separator distinguishes bookmarks from every other
  // record type.
  std::string hex = base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(
      "ahoi:bookmark:native-binding:v1:" + native_key)));
  hex.resize(32);
  hex[12] = '8';  // RFC 9562 UUIDv8: application-defined SHA-256 derivation.
  constexpr std::array<char, 4> kVariantDigits = {'8', '9', 'a', 'b'};
  hex[16] = kVariantDigits[
      (hex[16] >= 'a' ? hex[16] - 'a' + 10 : hex[16] - '0') & 3];
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}

std::optional<std::string> BookmarkSortKeyBetween(
    const std::string& lower,
    const std::optional<std::string>& upper) {
  if (upper && *upper <= lower) {
    return std::nullopt;
  }
  const auto valid = [](const std::string& value) {
    return value.size() <= 1024u &&
           std::ranges::all_of(
               value, [](unsigned char c) { return c >= '!' && c <= '~'; });
  };
  if (!valid(lower) || (upper && !valid(*upper))) {
    return std::nullopt;
  }
  std::string result;
  bool bounded = upper.has_value();
  for (size_t index = 0; index < 1024u; ++index) {
    const int lo = index < lower.size() ? lower[index] : ('!' - 1);
    const int hi =
        bounded ? (index < upper->size() ? (*upper)[index] : 0) : ('~' + 1);
    if (hi == 0) {
      return std::nullopt;
    }
    if (lo == hi) {
      result.push_back(static_cast<char>(lo));
      continue;
    }
    if (hi - lo > 1) {
      result.push_back(static_cast<char>((lo + hi) / 2));
      return result > lower && (!upper || result < *upper)
                 ? std::optional(result)
                 : std::nullopt;
    }
    if (lo < '!') {
      if (bounded && upper->size() > index + 1) {
        result.push_back('!');
        return result;
      }
      return std::nullopt;
    }
    result.push_back(static_cast<char>(lo));
    bounded = false;
  }
  return std::nullopt;
}

}  // namespace ahoi::sync
