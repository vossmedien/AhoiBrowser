// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/extension_setup_types.h"

#include <algorithm>

namespace ahoi::sync {

bool IsValidExtensionDesiredConfiguration(
    const ExtensionDesiredConfiguration& desired) {
  if (desired.extension_id.size() != 32 ||
      !std::ranges::all_of(desired.extension_id,
                           [](char c) { return c >= 'a' && c <= 'p'; }) ||
      (!desired.installed && desired.enabled)) {
    return false;
  }
  switch (desired.source) {
    case ExtensionInstallSource::kChromeWebStore:
      // The pinned Classic release has a different actual publisher identity;
      // it must never be fetched as an arbitrary/obsolete store package.
      return desired.extension_id != "fkgkibajhfbepljeaefdnfnegdcjomkh" &&
             desired.extension_id != "cjpalhdlnbpafiamejdnhcphjbkeiagm";
    case ExtensionInstallSource::kPinnedUblockClassic:
      return desired.extension_id == "fkgkibajhfbepljeaefdnfnegdcjomkh";
  }
  return false;
}

}  // namespace ahoi::sync
