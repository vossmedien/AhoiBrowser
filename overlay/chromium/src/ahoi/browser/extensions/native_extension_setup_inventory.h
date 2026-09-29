// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_INVENTORY_H_
#define AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_INVENTORY_H_

#include <optional>

#include "ahoi/browser/sync/extension_setup_types.h"

class Profile;
namespace extensions {
class Extension;
}

namespace ahoi::extensions {

// Native provenance only, not authority to modify or enable an extension.
std::optional<sync::ExtensionInstallSource> ReadNativeExtensionInstallSource(
    Profile* profile,
    const ::extensions::Extension& extension);

// Deferred during native startup/shutdown. Includes every installed ID in its
// local all-ID list, even when policy/source excludes it from restorable setup.
sync::NativeExtensionSetupSnapshot ReadNativeExtensionSetupInventory(
    Profile* profile);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_NATIVE_EXTENSION_SETUP_INVENTORY_H_
