// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/extension_storage_types.h"

#include <algorithm>
#include <array>

namespace ahoi::sync {
namespace {
constexpr std::string_view kVimiumId = "dbepggeogbaibhgnhhndojpepiihcmeb";
// philc/vimium v2.4.2 lib/settings.js: individual sync keys, true native bools,
// default pruning via Remove, and live storage.onChanged -> load/change.
// Native checks the installed package/version, never a peer's version claim.
constexpr auto kCatalog = std::to_array<ExtensionStorageDescriptor>({
    {kVimiumId, "2.4.2", "smoothScroll", true},
    {kVimiumId, "2.4.2", "filterLinkHints", false},
    {kVimiumId, "2.4.2", "hideHud", false},
    {kVimiumId, "2.4.2", "hideUpdateNotifications", false},
});
}  // namespace

base::span<const ExtensionStorageDescriptor> GetExtensionStorageCatalog() {
  return kCatalog;
}

const ExtensionStorageDescriptor* FindExtensionStorageSetting(
    std::string_view extension_id,
    std::string_view key) {
  const auto found =
      std::ranges::find_if(kCatalog, [&](const auto& descriptor) {
        return descriptor.extension_id == extension_id && descriptor.key == key;
      });
  return found == kCatalog.end() ? nullptr : &*found;
}

bool IsValidExtensionStorageValue(const ExtensionStorageValue& value) {
  return FindExtensionStorageSetting(value.extension_id, value.key) != nullptr;
}

}  // namespace ahoi::sync
