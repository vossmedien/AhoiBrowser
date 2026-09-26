// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// UboService's view of the installed uBlock Origin identities: inventory,
// committed authorization, the Lite-to-Classic migration and the
// ExtensionRegistry observers that keep them current (split from
// ubo_service.cc, source line budget).

#include <optional>
#include <string>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_service.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"

namespace ahoi::extensions {

void UboService::RefreshInventory() {
  status_.inventory = UboExtensionInventory();
  if (!profile_) {
    return;
  }
  if (auto* registry = ::extensions::ExtensionRegistry::Get(profile_)) {
    status_.inventory = ReadUboExtensionInventory(*registry);
  }
}

bool UboService::RefreshInstalledState() {
  RefreshInventory();
  status_.installed_version.clear();
  status_.authorized = false;
  if (!profile_) {
    return false;
  }
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_);
  if (!registry) {
    return false;
  }
  const ::extensions::Extension* installed =
      registry->GetInstalledExtension(kUboClassicExtensionId);
  if (!installed) {
    return false;
  }
  status_.installed_version = installed->version().GetString();
  // A pending install transaction may temporarily satisfy the runtime MV2
  // predicate. Periodic network activity requires the stronger condition: a
  // successfully committed authorization matching the installed extension.
  status_.authorized =
      ReadCommittedUboAuthorization(*profile_->GetPrefs()).has_value() &&
      IsUboManifestV2ExtensionAllowed(*profile_->GetPrefs(), *installed);
  return status_.authorized;
}

void UboService::RecomputeLiteMigrationState() {
  status_.lite_migration = UboLiteMigrationState::kNone;
  if (!profile_) {
    return;
  }
  // A failed write means there is no durable restart proof to re-read. Keep
  // that distinct failure blocked until a later explicit install succeeds;
  // never reinterpret the absent/malformed value as a neutral migration.
  if (status_.error == UboServiceError::kMigrationStateWriteFailed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    return;
  }
  if (status_.error == UboServiceError::kMigrationStateInvalid ||
      status_.error == UboServiceError::kLiteRemovalFailed) {
    status_.error = UboServiceError::kNone;
  }

  if (!profile_->GetPrefs()->FindPreference(kUboMigrationPref)) {
    return;
  }
  const base::DictValue& raw_migration =
      profile_->GetPrefs()->GetDict(kUboMigrationPref);
  std::optional<UboPersistedMigrationState> migration =
      ReadUboPersistedMigrationState(*profile_->GetPrefs());
  if (!migration) {
    if (!raw_migration.empty()) {
      status_.lite_migration = UboLiteMigrationState::kBlocked;
      status_.error = UboServiceError::kMigrationStateInvalid;
    }
    return;
  }
  std::optional<UboAuthorizationState> authorization =
      ReadCommittedUboAuthorization(*profile_->GetPrefs());
  if (!authorization ||
      !UboMigrationMatchesAuthorization(*migration, *authorization) ||
      !status_.inventory.classic.installed || !status_.authorized) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kMigrationStateInvalid;
    return;
  }
  if (lite_removal_in_progress_) {
    status_.lite_migration = UboLiteMigrationState::kRemovingLite;
    return;
  }
  if (!status_.inventory.lite.installed) {
    ClearUboPersistedMigrationState(profile_->GetPrefs());
    status_.lite_migration = UboLiteMigrationState::kComplete;
    return;
  }
  if (!status_.inventory.classic.enabled || !status_.inventory.classic.ready) {
    status_.lite_migration = UboLiteMigrationState::kClassicAwaitingReady;
    return;
  }
  status_.lite_migration = migration->install_process_token == process_token_
                               ? UboLiteMigrationState::kClassicAwaitingRestart
                               : UboLiteMigrationState::kEligibleForLiteRemoval;
}

bool UboService::IsRelevantUboIdentity(const std::string& extension_id) const {
  return extension_id == kUboClassicExtensionId ||
         extension_id == kUboFormerClassicWebStoreExtensionId ||
         extension_id == kUboLiteExtensionId;
}

void UboService::RequestRemoveUboLite() {
  if (IsBusy() || !profile_) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  if (status_.lite_migration !=
          UboLiteMigrationState::kEligibleForLiteRemoval ||
      !status_.inventory.classic.enabled || !status_.inventory.classic.ready ||
      !status_.inventory.lite.installed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
    return;
  }

  auto* registry = ::extensions::ExtensionRegistry::Get(profile_);
  auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_);
  if (!registry || !registrar ||
      !registry->GetInstalledExtension(kUboLiteExtensionId)) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
    return;
  }

  lite_removal_in_progress_ = true;
  status_.lite_migration = UboLiteMigrationState::kRemovingLite;
  status_.error = UboServiceError::kNone;
  Publish();

  std::u16string uninstall_error;
  if (!registrar->UninstallExtension(
          kUboLiteExtensionId, ::extensions::UNINSTALL_REASON_USER_INITIATED,
          &uninstall_error,
          base::BindOnce(&UboService::OnLiteUninstallCleanupComplete,
                         weak_factory_.GetWeakPtr()))) {
    lite_removal_in_progress_ = false;
    RefreshInstalledState();
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
  }
}

void UboService::OnLiteUninstallCleanupComplete() {
  lite_removal_in_progress_ = false;
  RefreshInstalledState();
  if (status_.inventory.lite.installed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
  } else {
    ClearUboPersistedMigrationState(profile_ ? profile_->GetPrefs() : nullptr);
    status_.lite_migration = UboLiteMigrationState::kComplete;
    status_.error = UboServiceError::kNone;
    status_.state = status_.authorized ? UboServiceState::kUpToDate
                                       : UboServiceState::kInstalled;
  }
  Publish();
}

void UboService::OnExtensionInstalled(content::BrowserContext* browser_context,
                                      const ::extensions::Extension* extension,
                                      bool is_update) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  MaybeStartPeriodicChecks();
  Publish();
}

void UboService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const ::extensions::Extension* extension,
    ::extensions::UninstallReason reason) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  if (extension->id() == kUboClassicExtensionId) {
    ClearCommittedUboAuthorization(profile_->GetPrefs());
    ClearUboPersistedMigrationState(profile_->GetPrefs());
    periodic_timer_.Stop();
    DeletePreparedPackage();
    status_.installed_version.clear();
    status_.authorized = false;
    status_.state = config_.IsProvisioned() ? UboServiceState::kIdle
                                            : UboServiceState::kUnprovisioned;
    status_.error = config_.IsProvisioned() ? UboServiceError::kNone
                                            : UboServiceError::kUnprovisioned;
    if (config_.IsPinnedBootstrapProvisioned()) {
      status_.catalog = GetPinnedUboBootstrapCatalogEntry();
    } else {
      status_.catalog.reset();
    }
  }
  RefreshInventory();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionLoaded(content::BrowserContext* browser_context,
                                   const ::extensions::Extension* extension) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionReady(content::BrowserContext* browser_context,
                                  const ::extensions::Extension* extension) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const ::extensions::Extension* extension,
    ::extensions::UnloadedExtensionReason reason) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnShutdown(::extensions::ExtensionRegistry* registry) {
  registry_observation_.Reset();
}

}  // namespace ahoi::extensions
