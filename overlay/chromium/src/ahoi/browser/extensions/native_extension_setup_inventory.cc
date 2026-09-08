// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/native_extension_setup_inventory.h"

#include <algorithm>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_product_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"

namespace ahoi::extensions {

std::optional<sync::ExtensionInstallSource> ReadNativeExtensionInstallSource(
    Profile* profile,
    const ::extensions::Extension& extension) {
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord() ||
      !extension.is_extension() ||
      extension.location() !=
          ::extensions::mojom::ManifestLocation::kInternal) {
    return std::nullopt;
  }
  if (extension.id() == kUboClassicExtensionId) {
    const auto* prefs = profile->GetPrefs();
    if (!prefs || !prefs->FindPreference(kUboAuthorizationPref)) {
      return std::nullopt;
    }
    // A pending package verification is not committed installed provenance.
    const auto committed = ReadCommittedUboAuthorization(*prefs);
    if (!committed || committed->extension_id != extension.id() ||
        committed->version != extension.version() ||
        !IsUboManifestV2ExtensionAllowed(*prefs, extension)) {
      return std::nullopt;
    }
    return sync::ExtensionInstallSource::kPinnedUblockClassic;
  }
  const sync::ExtensionDesiredConfiguration desired{
      .extension_id = extension.id(),
      .source = sync::ExtensionInstallSource::kChromeWebStore};
  return extension.from_webstore() &&
                 sync::IsValidExtensionDesiredConfiguration(desired)
             ? std::make_optional(desired.source)
             : std::nullopt;
}

sync::NativeExtensionSetupSnapshot ReadNativeExtensionSetupInventory(
    Profile* profile) {
  sync::NativeExtensionSetupSnapshot result;
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord()) {
    return result;
  }
  auto* system = ::extensions::ExtensionSystem::Get(profile);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile);
  auto* registrar = ::extensions::ExtensionRegistrar::Get(profile);
  auto* policy = system ? system->management_policy() : nullptr;
  if (!system || !system->is_ready() || !registry || !registrar || !policy) {
    return result;
  }
  const auto installed = registry->GenerateInstalledExtensionsSet();
  result.installed_extension_ids.reserve(installed.size());
  result.extensions.reserve(installed.size());
  for (const auto& extension : installed) {
    result.installed_extension_ids.push_back(extension->id());
    const auto source = ReadNativeExtensionInstallSource(profile, *extension);
    std::u16string policy_error;
    ::extensions::disable_reason::DisableReason disable_reason =
        ::extensions::disable_reason::DISABLE_NONE;
    if (!source ||
        !policy->UserMayModifySettings(extension.get(), &policy_error) ||
        policy->MustRemainInstalled(extension.get(), &policy_error) ||
        policy->MustRemainEnabled(extension.get(), &policy_error) ||
        policy->MustRemainDisabled(extension.get(), &disable_reason)) {
      continue;
    }
    result.extensions.push_back({
        .extension_id = extension->id(),
        .source = *source,
        .installed = true,
        // A crashed/terminated extension remains logically enabled in Chromium;
        // publishing it as a user disable would turn a runtime fault into
        // intent.
        .enabled = registrar->IsExtensionEnabled(extension->id()),
    });
  }
  std::ranges::sort(result.installed_extension_ids);
  std::ranges::sort(result.extensions, {},
                    &sync::ExtensionDesiredConfiguration::extension_id);
  result.complete = true;
  return result;
}

}  // namespace ahoi::extensions
