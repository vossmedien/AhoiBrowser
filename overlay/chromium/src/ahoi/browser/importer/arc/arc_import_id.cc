// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

bool IsBoundedUtf8(std::string_view value, size_t max_bytes) {
  return !value.empty() && value.size() <= max_bytes &&
         base::IsStringUTF8(value);
}

}  // namespace

base::Uuid MakeDeterministicArcId(std::string_view domain,
                                  std::string_view source_identifier) {
  if (domain.empty() || domain.size() > 64 ||
      !IsBoundedUtf8(source_identifier, kMaxSourceIdentifierBytes)) {
    return base::Uuid();
  }
  const std::array<uint8_t, crypto::hash::kSha256Size> digest =
      crypto::hash::Sha256(base::StrCat({domain, ":", source_identifier}));
  std::array<uint8_t, 16> uuid_bytes;
  std::copy_n(digest.begin(), uuid_bytes.size(), uuid_bytes.begin());
  uuid_bytes[6] = static_cast<uint8_t>((uuid_bytes[6] & 0x0f) | 0x50);
  uuid_bytes[8] = static_cast<uint8_t>((uuid_bytes[8] & 0x3f) | 0x80);

  const std::string hex = base::HexEncodeLower(uuid_bytes);
  return base::Uuid::ParseLowercase(base::StrCat(
      {hex.substr(0, 8), "-", hex.substr(8, 4), "-", hex.substr(12, 4), "-",
       hex.substr(16, 4), "-", hex.substr(20, 12)}));
}

}  // namespace ahoi::importer::arc
