// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

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
         profile_->GetPrefs()->GetBoolean(kRemoteControlEnabledPref);
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
    if (id.is_valid() && value.is_string()) {
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
    if (value.is_string() && IsPermittedProductSettingId(value.GetString())) {
      result.push_back(value.GetString());
    }
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool ProfileSyncService::SetPermittedSettingSyncEnabled(std::string setting_id,
                                                        bool enabled) {
  if (!profile_ || shutting_down_ || !IsPermittedProductSettingId(setting_id)) {
    return false;
  }
  SetListMembership(profile_->GetPrefs(), kPermittedSettingIdsPref, setting_id,
                    enabled);
  if (enabled) {
    const auto stored = std::ranges::find(permitted_settings_, setting_id,
                                          &PermittedSettingRecord::setting_id);
    if (stored != permitted_settings_.end() && !stored->tombstone &&
        ApplyPermittedProductSetting(profile_->GetPrefs(), stored->setting_id,
                                     stored->value_json)) {
      return true;
    }
    PublishPermittedProductSetting(std::move(setting_id));
    return true;
  }
  const auto stored = std::ranges::find(permitted_settings_, setting_id,
                                        &PermittedSettingRecord::setting_id);
  if (stored != permitted_settings_.end() && !stored->tombstone) {
    PermittedSettingRecord tombstone = *stored;
    tombstone.tombstone = true;
    backend_.AsyncCall(&ProfileSyncBackend::UpsertPermittedSetting)
        .WithArgs(std::move(tombstone))
        .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                             weak_ptr_factory_.GetWeakPtr()));
  }
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
  if (!enabled) {
    const auto stored = std::ranges::find(developer_assets_, asset_id,
                                          &DeveloperAssetRecord::id);
    if (stored != developer_assets_.end() && !stored->tombstone) {
      DeveloperAssetRecord tombstone = *stored;
      tombstone.opted_in = false;
      tombstone.tombstone = true;
      backend_.AsyncCall(&ProfileSyncBackend::UpsertDeveloperAsset)
          .WithArgs(std::move(tombstone))
          .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                               weak_ptr_factory_.GetWeakPtr()));
    }
  }
  return true;
}

bool ProfileSyncService::PublishDeveloperAsset(DeveloperAssetRecord record) {
  if (!profile_ || shutting_down_ || !record.id.is_valid() ||
      !ListContains(profile_->GetPrefs()->GetList(kDeveloperAssetOptInIdsPref),
                    record.id.AsLowercaseString())) {
    return false;
  }
  record.opted_in = true;
  record.tombstone = false;
  backend_.AsyncCall(&ProfileSyncBackend::UpsertDeveloperAsset)
      .WithArgs(std::move(record))
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void ProfileSyncService::InitializeProductSync() {
  if (!profile_) {
    return;
  }
  for (std::string_view setting_id : GetPermittedProductSettingIds()) {
    sync_pref_registrar_.Add(
        std::string(setting_id),
        base::BindRepeating(
            &ProfileSyncService::OnPermittedProductSettingChanged,
            weak_ptr_factory_.GetWeakPtr(), std::string(setting_id)));
  }
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
  if (extension_registry_) {
    extension_registry_->RemoveObserver(this);
    extension_registry_ = nullptr;
  }
}

void ProfileSyncService::ApplyProductState(const SyncStateSnapshot& state) {
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

  const base::ListValue& enabled_settings =
      profile_->GetPrefs()->GetList(kPermittedSettingIdsPref);
  applying_product_state_ = true;
  for (const PermittedSettingRecord& record : state.permitted_settings) {
    if (record.tombstone ||
        !ListContains(enabled_settings, record.setting_id) ||
        applied_setting_versions_[record.id] >= record.version) {
      continue;
    }
    if (ApplyPermittedProductSetting(profile_->GetPrefs(), record.setting_id,
                                     record.value_json)) {
      applied_setting_versions_[record.id] = record.version;
    }
  }
  applying_product_state_ = false;

  if (!extension_inventory_seeded_) {
    extension_inventory_seeded_ = true;
    PublishExtensionInventory();
  }
}

void ProfileSyncService::PublishCurrentAppearance() {
  if (!profile_ || shutting_down_ || applying_product_state_ ||
      appearance_publish_pending_ || !backend_ready_) {
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
          weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::PublishPermittedProductSetting(
    std::string setting_id) {
  if (!profile_ || shutting_down_ || applying_product_state_ ||
      !ListContains(profile_->GetPrefs()->GetList(kPermittedSettingIdsPref),
                    setting_id)) {
    return;
  }
  std::optional<std::string> value =
      EncodePermittedProductSetting(*profile_->GetPrefs(), setting_id);
  if (!value) {
    return;
  }
  PermittedSettingRecord record{
      .id = StableProductRecordId("setting", setting_id),
      .setting_id = std::move(setting_id),
      .value_json = std::move(*value)};
  backend_.AsyncCall(&ProfileSyncBackend::UpsertPermittedSetting)
      .WithArgs(std::move(record))
      .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                           weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnPermittedProductSettingChanged(
    std::string setting_id) {
  PublishPermittedProductSetting(std::move(setting_id));
}

void ProfileSyncService::PublishExtensionInventory() {
  if (!extension_registry_ || !backend_ready_ || shutting_down_) {
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
                           weak_ptr_factory_.GetWeakPtr()));
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
