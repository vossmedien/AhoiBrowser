// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_EXTENSION_INVENTORY_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_EXTENSION_INVENTORY_H_

#include <string>

namespace extensions {
class ExtensionRegistry;
}

namespace ahoi::extensions {

struct UboExtensionState {
  bool installed = false;
  bool enabled = false;
  bool ready = false;
  std::string version;
};

// The three identities are deliberately separate. In particular, an installed
// uBO Lite/MV3 instance or the former Web Store identity can never satisfy the
// pinned Classic authorization or runtime gates.
struct UboExtensionInventory {
  UboExtensionState classic;
  UboExtensionState former_classic_web_store;
  UboExtensionState lite;
};

UboExtensionInventory ReadUboExtensionInventory(
    const ::extensions::ExtensionRegistry& registry);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_EXTENSION_INVENTORY_H_
