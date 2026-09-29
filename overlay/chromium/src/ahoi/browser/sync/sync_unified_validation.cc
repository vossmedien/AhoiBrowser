// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_unified_validation.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "ahoi/browser/sync/sync_merge.h"
#include "base/hash/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace ahoi::sync {

base::Uuid CapabilityIdForDevice(const base::Uuid& device_id) {
  if (!IsCanonicalSyncDeviceId(device_id.AsLowercaseString())) {
    return {};
  }
  // RFC UUIDv5 URL namespace. SHA-1 is the UUID identity algorithm here,
  // never a signature, password hash or encryption primitive.
  constexpr std::array<unsigned char, 16> kNamespace = {
      0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
      0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
  std::string material;
  for (unsigned char value : kNamespace) {
    material.push_back(static_cast<char>(value));
  }
  material += "ahoi:sync:capability:v1:" + device_id.AsLowercaseString();
  std::string digest = base::SHA1HashString(material);
  digest.resize(16);
  digest[6] =
      static_cast<char>((static_cast<unsigned char>(digest[6]) & 0x0f) | 0x50);
  digest[8] =
      static_cast<char>((static_cast<unsigned char>(digest[8]) & 0x3f) | 0x80);
  const std::string hex = base::ToLowerASCII(base::HexEncode(digest));
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}

bool ValidateCapability(const DeviceCapabilityRecord& record) {
  const std::string device = record.device_id.AsLowercaseString();
  if (!IsCanonicalSyncDeviceId(device) ||
      device == "9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86" ||
      record.id != CapabilityIdForDevice(record.device_id) ||
      record.readable_models != std::vector<int>{kCurrentModelVersion} ||
      record.writable_models != std::vector<int>{kCurrentModelVersion} ||
      record.features.size() > 32 || !std::ranges::is_sorted(record.features) ||
      std::adjacent_find(record.features.begin(), record.features.end()) !=
          record.features.end() ||
      record.version.stamp.device_tiebreak != device) {
    return false;
  }
  for (const auto& feature : record.features) {
    if (feature.empty() || feature.size() > 64 ||
        !std::ranges::all_of(feature, [](unsigned char value) {
          return value >= 0x21 && value <= 0x7e;
        })) {
      return false;
    }
  }
  for (const auto& [field, clock] : record.field_versions) {
    if (clock.device_tiebreak != device) {
      return false;
    }
  }
  return true;
}

bool ValidateSharedTarget(const std::string& url,
                          const std::optional<SharedTabTargetKind>& kind,
                          const std::optional<std::string>& local_scheme) {
  if (!kind) {
    return false;
  }
  switch (*kind) {
    case SharedTabTargetKind::kWeb: {
      if (local_scheme || url.empty() || url.size() > 131072 ||
          !base::IsStringUTF8(url) ||
          std::ranges::any_of(
              url, [](unsigned char c) { return c <= 0x20 || c == 0x7f; })) {
        return false;
      }
      const GURL parsed(url);
      return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
             !parsed.host().empty() && !parsed.has_username() &&
             !parsed.has_password();
    }
    case SharedTabTargetKind::kNewTab:
      return url.empty() && !local_scheme;
    case SharedTabTargetKind::kLocalOnly: {
      constexpr std::array<std::string_view, 8> kSchemes = {
          "about", "chrome", "chrome-extension", "file",
          "blob",  "data",   "javascript",       "other"};
      return url.empty() && local_scheme &&
             std::ranges::contains(kSchemes, *local_scheme);
    }
  }
  return false;
}

bool SharedPresenceMatchesPage(const RemoteTabRecord& presence,
                               const TreeNodeRecord& page) {
  if (presence.tombstone) {
    // A well-formed Presence delete must not wait for a live Page. Store/wire
    // callers validate the record itself before this relational check.
    return true;
  }
  return presence.tree_node_id == page.id && !page.tombstone &&
         page.kind == TreeNodeKind::kPage && presence.url == page.url &&
         presence.target_kind == page.target_kind &&
         presence.local_scheme == page.local_scheme &&
         (page.target_kind != SharedTabTargetKind::kNewTab ||
          page.is_temporary);
}

}  // namespace ahoi::sync
