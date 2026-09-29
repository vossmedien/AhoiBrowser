// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_extension_inventory.h"

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"

namespace ahoi::extensions {

namespace {

UboExtensionState ReadExtensionState(
    const ::extensions::ExtensionRegistry& registry,
    const std::string& extension_id) {
  UboExtensionState state;
  const ::extensions::Extension* extension =
      registry.GetInstalledExtension(extension_id);
  if (!extension) {
    return state;
  }
  state.installed = true;
  state.enabled = registry.enabled_extensions().Contains(extension_id);
  state.ready = registry.ready_extensions().Contains(extension_id);
  state.version = extension->version().GetString();
  return state;
}

}  // namespace

UboExtensionInventory ReadUboExtensionInventory(
    const ::extensions::ExtensionRegistry& registry) {
  return {
      .classic = ReadExtensionState(registry, kUboClassicExtensionId),
      .former_classic_web_store =
          ReadExtensionState(registry, kUboFormerClassicWebStoreExtensionId),
      .lite = ReadExtensionState(registry, kUboLiteExtensionId),
  };
}

}  // namespace ahoi::extensions
