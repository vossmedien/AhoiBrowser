// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/native_search_engine_setting.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_product_settings.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "crypto/sha2.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"

namespace ahoi::sync {
namespace {

base::Uuid StableProductRecordId(std::string_view category,
                                 std::string_view stable_key) {
  std::string material(category);
  material.push_back(':');
  material.append(stable_key);
  std::string hex =
      base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(material)));
  hex.resize(32);
  hex[12] = '4';
  hex[16] = '8';
  return base::Uuid::ParseLowercase(hex.substr(0, 8) + "-" + hex.substr(8, 4) +
                                    "-" + hex.substr(12, 4) + "-" +
                                    hex.substr(16, 4) + "-" + hex.substr(20));
}

bool ListContains(const base::ListValue& values, std::string_view needle) {
  return std::ranges::any_of(values, [needle](const base::Value& value) {
    return value.is_string() && value.GetString() == needle;
  });
}

void SetListMembership(PrefService* prefs,
                       std::string_view pref_name,
                       std::string value,
                       bool enabled) {
  base::ListValue values = prefs->GetList(pref_name).Clone();
  values.EraseValue(base::Value(value));
  if (enabled) {
    values.Append(std::move(value));
  }
  prefs->SetList(pref_name, std::move(values));
}

std::string ColorModeName(ThemeService::BrowserColorScheme mode) {
  switch (mode) {
    case ThemeService::BrowserColorScheme::kSystem:
      return "system";
    case ThemeService::BrowserColorScheme::kLight:
      return "light";
    case ThemeService::BrowserColorScheme::kDark:
      return "dark";
  }
}

std::optional<ThemeService::BrowserColorScheme> ParseColorMode(
    std::string_view mode) {
  if (mode == "system") {
    return ThemeService::BrowserColorScheme::kSystem;
  }
  if (mode == "light") {
    return ThemeService::BrowserColorScheme::kLight;
  }
  if (mode == "dark") {
    return ThemeService::BrowserColorScheme::kDark;
  }
  return std::nullopt;
}

}  // namespace

bool ProfileSyncService::remote_control_enabled() const {
  return profile_ && !shutting_down_ &&
         remote_control_prerequisite() == RemoteControlPrerequisite::kReady &&
         profile_->GetPrefs()->GetBoolean(kRemoteControlEnabledPref);
}

ProfileSyncService::RemoteControlPrerequisite
ProfileSyncService::remote_control_prerequisite() const {
  if (!profile_ || shutting_down_ || !sync_enabled_) {
    return RemoteControlPrerequisite::kSyncDisabled;
  }
  if (!initialized_ || backend_.is_null() || !transport_status_.enabled ||
      !transport_status_.provider_available) {
    return RemoteControlPrerequisite::kTransportUnavailable;
  }
  if (transport_status_.account_transition_pending ||
      transport_status_.zone_recovery_pending) {
    return RemoteControlPrerequisite::kRecoveryPending;
  }
  if (approved_remote_control_devices().empty()) {
    return RemoteControlPrerequisite::kApprovedDeviceRequired;
  }
  return RemoteControlPrerequisite::kReady;
}

bool ProfileSyncService::can_pair_remote_control_device() const {
  if (!profile_ || shutting_down_ || !sync_enabled_ || !initialized_ ||
      backend_.is_null() || !transport_status_.enabled ||
      !transport_status_.provider_available) {
    return false;
  }
  return !transport_status_.account_transition_pending &&
         !transport_status_.zone_recovery_pending;
}

int ProfileSyncService::history_retention_days() const {
  return profile_ && !shutting_down_
             ? profile_->GetPrefs()->GetInteger(kHistoryRetentionDaysPref)
             : 0;
}

std::vector<base::Uuid> ProfileSyncService::approved_remote_control_devices()
    const {
  std::vector<base::Uuid> result;
  if (!profile_ || shutting_down_) {
    return result;
  }
  for (const auto [key, value] :
       profile_->GetPrefs()->GetDict(kApprovedRemoteCommandKeysPref)) {
    const base::Uuid id = base::Uuid::ParseLowercase(key);
    if (id.is_valid() && value.is_string() &&
        IsValidRemoteControlPublicKeyBase64(value.GetString())) {
      result.push_back(id);
    }
  }
  std::ranges::sort(result);
  return result;
}

std::vector<std::string> ProfileSyncService::permitted_setting_ids() const {
  std::vector<std::string> result;
  if (!profile_ || shutting_down_) {
    return result;
  }
  for (const base::Value& value :
       profile_->GetPrefs()->GetList(kPermittedSettingIdsPref)) {
    if (value.is_string() && SupportsBrowserSetting(value.GetString())) {
      result.push_back(value.GetString());
    }
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool ProfileSyncService::SetPermittedSettingSyncEnabled(std::string setting_id,
                                                        bool enabled) {
  if (!profile_ || shutting_down_ || !SupportsBrowserSetting(setting_id)) {
    return false;
  }
  SetListMembership(profile_->GetPrefs(), kPermittedSettingIdsPref, setting_id,
                    enabled);
  // The consent observer revokes queued work and reads the existing shared
  // value first. Local opt-out is NOT a global deletion or a default reset.
  return true;
}

std::vector<std::string> ProfileSyncService::supported_setting_ids() const {
  std::vector<std::string> result;
  if (!profile_ || shutting_down_) {
    return result;
  }
  for (std::string_view id : GetPermittedProductSettingIds()) {
    if (SupportsBrowserSetting(id)) {
      result.emplace_back(id);
    }
  }
  return result;
}

bool ProfileSyncService::SetBrowserSettingsSyncEnabled(bool enabled) {
  if (!profile_ || shutting_down_) {
    return false;
  }
  PrefService* prefs = profile_->GetPrefs();
  auto selected = prefs->GetList(kPermittedSettingIdsPref).Clone();
  for (const auto& id : supported_setting_ids()) {
    selected.EraseValue(base::Value(id));
    if (enabled) {
      selected.Append(id);
    }
  }
  prefs->SetList(kPermittedSettingIdsPref, std::move(selected));
  return true;
}

bool ProfileSyncService::SetDeveloperAssetSyncEnabled(
    const base::Uuid& asset_id,
    bool enabled) {
  if (!profile_ || shutting_down_ || !asset_id.is_valid()) {
    return false;
  }
  SetListMembership(profile_->GetPrefs(), kDeveloperAssetOptInIdsPref,
                    asset_id.AsLowercaseString(), enabled);
  if (!enabled && sync_enabled_ && !backend_.is_null()) {
    const auto stored = std::ranges::find(developer_assets_, asset_id,
                                          &DeveloperAssetRecord::id);
    if (stored != developer_assets_.end() && !stored->tombstone) {
      DeveloperAssetRecord tombstone = *stored;
      tombstone.opted_in = false;
      tombstone.tombstone = true;
      backend_.AsyncCall(&ProfileSyncBackend::UpsertDeveloperAsset)
          .WithArgs(std::move(tombstone))
          .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                               backend_weak_ptr_factory_.GetWeakPtr()));
    }
  }
  return true;
}

bool ProfileSyncService::PublishDeveloperAsset(DeveloperAssetRecord record) {
  if (!profile_ || shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      !record.id.is_valid() ||
      !ListContains(profile_->GetPrefs()->GetList(kDeveloperAssetOptInIdsPref),
                    record.id.AsLowercaseString())) {
    return false;
  }
  record.opted_in = true;
  record.tombstone = false;
  backend_.AsyncCall(&ProfileSyncBackend::UpsertDeveloperAsset)
      .WithArgs(std::move(record))
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void ProfileSyncService::InitializeProductSync() {
  if (!profile_) {
    return;
  }
  InitializeBrowserSettings();
  sync_pref_registrar_.Add(
      prefs::kBrowserColorScheme,
      base::BindRepeating(&ProfileSyncService::PublishCurrentAppearance,
                          weak_ptr_factory_.GetWeakPtr()));
  sync_pref_registrar_.Add(
      prefs::kUserColor,
      base::BindRepeating(&ProfileSyncService::PublishCurrentAppearance,
                          weak_ptr_factory_.GetWeakPtr()));
  extension_registry_ = extensions::ExtensionRegistry::Get(profile_);
  if (extension_registry_) {
    extension_registry_->AddObserver(this);
  }
}

void ProfileSyncService::ShutdownProductSync() {
  native_search_engine_setting_.reset();
  if (extension_registry_) {
    extension_registry_->RemoveObserver(this);
    extension_registry_ = nullptr;
  }
}

void ProfileSyncService::ApplyProductState(const SyncStateSnapshot& state) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null()) {
    return;
  }
  permitted_settings_ = state.permitted_settings;
  extension_inventory_ = state.extension_inventory;
  developer_assets_ = state.developer_assets;
  if (!profile_ || shutting_down_) {
    return;
  }

  const auto appearance = std::ranges::max_element(state.appearance, {},
                                                   &AppearanceRecord::version);
  if (appearance == state.appearance.end()) {
    PublishCurrentAppearance();
  } else if (!appearance->tombstone &&
             (applied_appearance_versions_[appearance->id] <
              appearance->version)) {
    const std::optional<ThemeService::BrowserColorScheme> mode =
        ParseColorMode(appearance->color_mode);
    ThemeService* theme = ThemeServiceFactory::GetForProfile(profile_);
    if (mode && theme) {
      applying_product_state_ = true;
      theme->SetBrowserColorScheme(*mode);
      theme->SetUserColor(appearance->use_system_accent
                              ? std::nullopt
                              : appearance->accent_argb);
      applying_product_state_ = false;
      applied_appearance_versions_[appearance->id] = appearance->version;
    }
  }

  RefreshBrowserSettings();

  if (!extension_inventory_seeded_) {
    extension_inventory_seeded_ = true;
    PublishExtensionInventory();
  }
}

void ProfileSyncService::PublishCurrentAppearance() {
  if (!profile_ || shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      applying_product_state_ || appearance_publish_pending_ ||
      !backend_ready_) {
    return;
  }
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile_);
  if (!theme) {
    return;
  }
  const std::optional<SkColor> user_color = theme->GetUserColor();
  AppearanceRecord record{
      .id = StableProductRecordId("appearance", "profile"),
      .color_mode = ColorModeName(theme->GetBrowserColorScheme()),
      .accent_argb = user_color,
      .use_system_accent = !user_color.has_value()};
  appearance_publish_pending_ = true;
  backend_.AsyncCall(&ProfileSyncBackend::UpsertAppearance)
      .WithArgs(std::move(record))
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> service,
             std::optional<SyncStateSnapshot> state) {
            if (!service) {
              return;
            }
            service->appearance_publish_pending_ = false;
            service->OnBackendState(std::move(state));
            service->SyncNow();
          },
          backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::PublishExtensionInventory() {
  if (!extension_registry_ || !backend_ready_ || shutting_down_ ||
      !sync_enabled_ || backend_.is_null()) {
    return;
  }
  std::vector<ExtensionInventoryRecord> records;
  const extensions::ExtensionSet installed =
      extension_registry_->GenerateInstalledExtensionsSet();
  for (const scoped_refptr<const extensions::Extension>& extension :
       installed) {
    if (!extension->is_extension()) {
      continue;
    }
    records.push_back(
        {.id = StableProductRecordId(
             "extension",
             local_device_id_.AsLowercaseString() + ":" + extension->id()),
         .device_id = local_device_id_,
         .extension_id = extension->id(),
         .name = extension->name(),
         .extension_version = extension->VersionString(),
         .enabled = extension_registry_->enabled_extensions().Contains(
             extension->id())});
  }
  backend_.AsyncCall(&ProfileSyncBackend::ReplaceLocalExtensionInventory)
      .WithArgs(std::move(records))
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension) {
  std::ignore = browser_context;
  std::ignore = extension;
  PublishExtensionInventory();
}

void ProfileSyncService::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  std::ignore = browser_context;
  std::ignore = extension;
  std::ignore = reason;
  PublishExtensionInventory();
}

void ProfileSyncService::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    bool is_update) {
  std::ignore = browser_context;
  std::ignore = extension;
  std::ignore = is_update;
  PublishExtensionInventory();
}

void ProfileSyncService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  std::ignore = browser_context;
  std::ignore = extension;
  std::ignore = reason;
  PublishExtensionInventory();
}

void ProfileSyncService::OnShutdown(extensions::ExtensionRegistry* registry) {
  if (extension_registry_ == registry) {
    extension_registry_ = nullptr;
  }
}

}  // namespace ahoi::sync
