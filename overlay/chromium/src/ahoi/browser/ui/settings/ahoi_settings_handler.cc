// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

namespace ahoi::settings {
namespace {

const char* PrerequisiteName(
    sync::ProfileSyncService::RemoteControlPrerequisite prerequisite) {
  using Prerequisite = sync::ProfileSyncService::RemoteControlPrerequisite;
  switch (prerequisite) {
    case Prerequisite::kReady:
      return "ready";
    case Prerequisite::kSyncDisabled:
      return "syncDisabled";
    case Prerequisite::kTransportUnavailable:
      return "transportUnavailable";
    case Prerequisite::kRecoveryPending:
      return "recoveryPending";
    case Prerequisite::kApprovedDeviceRequired:
      return "approvedDeviceRequired";
  }
  return "transportUnavailable";
}

bool HasCallbackId(const base::ListValue& args) {
  return !args.empty() && args.front().is_string();
}

std::string SyncStatusLabel(const sync::SyncTransportStatus& status) {
  const bool german = base::i18n::GetConfiguredLocale().starts_with("de");
  const auto text = [german](const char* de, const char* en) {
    return std::string(german ? de : en);
  };
  if (!status.enabled) {
    return text("Sync ist ausgeschaltet", "Sync is off");
  }
  if (status.account_transition_pending) {
    return text("iCloud-Accountwechsel benötigt Bestätigung",
                "iCloud account change needs confirmation");
  }
  if (status.zone_recovery_pending) {
    return text("Sync-Zone benötigt Wiederherstellung",
                "Sync zone needs recovery");
  }
  if (status.key_setup_issue == "key_setup_waiting_for_key") {
    return text("Warten auf den gemeinsamen iCloud-Schlüssel",
                "Waiting for the shared iCloud key");
  }
  if (status.key_setup_issue == "key_setup_in_progress" ||
      status.key_setup_issue == "key_setup_busy") {
    return text("Sync-Verbindung wird eingerichtet",
                "Setting up the sync connection");
  }
  if (!status.key_setup_issue.empty()) {
    return text("Sync-Einrichtung unterbrochen", "Sync setup interrupted");
  }
  if (!status.provider_available) {
    return text("Nur lokal · CloudKit nicht verfügbar",
                "Local only · CloudKit unavailable");
  }
  if (status.retry.attempt > 0) {
    return text("Neuer Sync-Versuch geplant", "Sync retry scheduled");
  }
  if (status.pending_outbox > 0) {
    return text("Änderungen warten auf Abgleich",
                "Changes are waiting to sync");
  }
  return text("Synchronisiert und bereit", "Synced and ready");
}

std::string ExtensionResultLabel(sync::ExtensionRestoreDisposition result) {
  const bool german = base::i18n::GetConfiguredLocale().starts_with("de");
  switch (result) {
    case sync::ExtensionRestoreDisposition::kApplied:
      return german ? "Übernommen" : "Applied";
    case sync::ExtensionRestoreDisposition::kNeedsConfirmation:
      return german ? "Bestätigung nötig" : "Confirmation required";
    case sync::ExtensionRestoreDisposition::kPending:
      return german ? "Wird vorbereitet" : "Pending";
    case sync::ExtensionRestoreDisposition::kBlockedByPolicy:
      return german ? "Durch Richtlinie gesperrt" : "Blocked by policy";
    case sync::ExtensionRestoreDisposition::kUnsupported:
      return german ? "Separat einrichten" : "Set up separately";
    case sync::ExtensionRestoreDisposition::kFailed:
      return german ? "Fehlgeschlagen" : "Failed";
    case sync::ExtensionRestoreDisposition::kCancelled:
      return german ? "Abgebrochen" : "Cancelled";
  }
  return {};
}

}  // namespace

AhoiSettingsHandler::AhoiSettingsHandler(Profile* profile)
    : profile_(profile),
      sync_service_(sync::ProfileSyncServiceFactory::GetForProfile(profile_)) {}

AhoiSettingsHandler::~AhoiSettingsHandler() {
  if (sync_service_ && observing_sync_service_) {
    sync_service_->RemoveObserver(this);
  }
}

void AhoiSettingsHandler::RegisterMessages() {
  if (sync_service_ && !observing_sync_service_) {
    observing_sync_service_ = true;
    sync_service_->AddObserver(this);
  }
  web_ui()->RegisterMessageCallback(
      "ahoiGetBrowserSettingsSyncStatus",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleGetBrowserSettingsSyncStatus,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiSetBrowserSettingsSyncEnabled",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleSetBrowserSettingsSyncEnabled,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiGetSyncControlsStatus",
      base::BindRepeating(&AhoiSettingsHandler::HandleGetSyncControlsStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiSyncControlAction",
      base::BindRepeating(&AhoiSettingsHandler::HandleSyncControlAction,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiGetRemoteControlStatus",
      base::BindRepeating(&AhoiSettingsHandler::HandleGetRemoteControlStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiSetRemoteControlEnabled",
      base::BindRepeating(&AhoiSettingsHandler::HandleSetRemoteControlEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiApproveRemoteControlDevice",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleApproveRemoteControlDevice,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiRevokeRemoteControlDevice",
      base::BindRepeating(&AhoiSettingsHandler::HandleRevokeRemoteControlDevice,
                          base::Unretained(this)));
}

void AhoiSettingsHandler::OnAhoiDeviceTabsChanged(
    const sync::DeviceTabsSnapshot& /*snapshot*/) {}

void AhoiSettingsHandler::OnAhoiSyncStatusChanged(
    const sync::SyncTransportStatus& /*status*/) {
  PushStatus({});
  PushBrowserSettingsSyncStatus();
  PushSyncControlsStatus();
}

bool AhoiSettingsHandler::IsAuthorizedSettingsPage() {
  if (!profile_ || profile_->IsOffTheRecord() ||
      !profile_->IsRegularProfile() || !profile_->AllowsBrowserWindows() ||
      !web_ui()) {
    return false;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  if (!contents || contents->GetBrowserContext() != profile_) {
    return false;
  }
  const GURL& url = contents->GetLastCommittedURL();
  return url.SchemeIs("chrome") && url.host() == "settings";
}

base::DictValue AhoiSettingsHandler::BuildBrowserSettingsSyncStatus(
    std::string_view action) const {
  base::DictValue result;
  result.Set("action", std::string(action));
  const bool available =
      sync_service_ && profile_ && profile_->IsRegularProfile() &&
      !profile_->IsOffTheRecord() && profile_->AllowsBrowserWindows();
  const auto supported = available ? sync_service_->supported_setting_ids()
                                   : std::vector<std::string>();
  const auto permitted = available ? sync_service_->permitted_setting_ids()
                                   : std::vector<std::string>();
  int selected_count = 0;
  for (const std::string& id : supported) {
    if (std::ranges::contains(permitted, id)) {
      ++selected_count;
    }
  }
  const int supported_count = static_cast<int>(supported.size());
  result.Set("supportedCount", supported_count);
  result.Set("selectedCount", selected_count);
  result.Set("selection", selected_count == 0                 ? "none"
                          : selected_count == supported_count ? "all"
                                                              : "some");
  result.Set("canChange", available && supported_count > 0);
  result.Set("syncEnabled", available && sync_service_->sync_enabled());
  return result;
}

void AhoiSettingsHandler::ResolveBrowserSettingsSyncStatus(
    base::Value callback_id,
    std::string_view action) {
  ResolveJavascriptCallback(
      callback_id, base::Value(BuildBrowserSettingsSyncStatus(action)));
}

void AhoiSettingsHandler::PushBrowserSettingsSyncStatus() {
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  FireWebUIListener("ahoi-browser-settings-sync-status-changed",
                    base::Value(BuildBrowserSettingsSyncStatus({})));
}

void AhoiSettingsHandler::HandleGetBrowserSettingsSyncStatus(
    const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args) ||
      args.front().GetString().empty() || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  ResolveBrowserSettingsSyncStatus(args.front().Clone(), {});
}

void AhoiSettingsHandler::HandleSetBrowserSettingsSyncEnabled(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || args.front().GetString().empty() ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  if (args.size() != 2u || !args[1].is_bool()) {
    ResolveBrowserSettingsSyncStatus(args.front().Clone(), "invalidRequest");
    return;
  }
  const bool enabled = args[1].GetBool();
  // This deliberate category action does not enable global Sync or bypass
  // the service's account, per-setting or transport authorization boundaries.
  const bool changed = sync_service_ &&
                       !sync_service_->supported_setting_ids().empty() &&
                       sync_service_->SetBrowserSettingsSyncEnabled(enabled);
  ResolveBrowserSettingsSyncStatus(
      args.front().Clone(),
      changed ? (enabled ? "enabled" : "disabled") : "blocked");
}

base::DictValue AhoiSettingsHandler::BuildSyncControlsStatus(
    std::string_view action) const {
  base::DictValue result;
  result.Set("action", std::string(action));
  const bool german = base::i18n::GetConfiguredLocale().starts_with("de");
  const auto text = [german](const char* de, const char* en) {
    return std::string(german ? de : en);
  };
  base::DictValue labels;
  labels.Set("syncNow", text("Jetzt synchronisieren", "Sync now"));
  labels.Set("retryKey", text("Verbindung erneut prüfen", "Check connection again"));
  labels.Set("extensions", text("Erweiterungen", "Extensions"));
  labels.Set("extensionSetup",
             text("Erweiterungen synchronisieren", "Sync extensions"));
  labels.Set("extensionSettings",
             text("Unterstützte Erweiterungsoptionen",
                  "Supported extension settings"));
  labels.Set("extensionSettingsHint",
             text("Nur geprüfte Optionen; unbekannte Daten bleiben lokal.",
                  "Only reviewed options; unknown data stays local."));
  labels.Set("retryExtension", text("Erneut versuchen", "Try again"));
  labels.Set("reviewExtension", text("Einrichten…", "Set up…"));
  labels.Set("recovery", text("Sync-Wiederherstellung", "Sync recovery"));
  labels.Set("accountRecoveryHint",
             text("Wähle bewusst, ob lokale Änderungen mit dem aktuellen "
                  "iCloud-Account zusammengeführt werden.",
                  "Choose whether to merge local changes into the current "
                  "iCloud account."));
  labels.Set("uploadLocal",
             text("Lokale Daten weiter hochladen", "Continue uploading local data"));
  labels.Set("withoutUpload",
             text("Ohne lokalen Upload fortfahren", "Continue without local upload"));
  labels.Set("recoverZone",
             text("Sync-Zone wiederherstellen", "Recover sync zone"));
  result.Set("labels", std::move(labels));

  const sync::SyncTransportStatus status =
      sync_service_ ? sync_service_->transport_status()
                    : sync::SyncTransportStatus();
  result.Set("statusLabel", SyncStatusLabel(status));
  result.Set("syncEnabled", status.enabled);
  result.Set("providerAvailable", status.provider_available);
  result.Set("keySetupIssue", status.key_setup_issue);
  result.Set("accountTransitionPending", status.account_transition_pending);
  result.Set("zoneRecoveryPending", status.zone_recovery_pending);
  result.Set("canSyncNow", sync_service_ && status.enabled &&
                               status.provider_available &&
                               status.key_setup_issue.empty() &&
                               !status.account_transition_pending &&
                               !status.zone_recovery_pending);
  result.Set("canRetryKey", sync_service_ && status.enabled &&
                                !status.key_setup_issue.empty() &&
                                !status.account_transition_pending &&
                                !status.zone_recovery_pending);
  result.Set("canChangeExtensionConsent", sync_service_ != nullptr);
  result.Set("extensionSetupEnabled",
             sync_service_ && sync_service_->extension_setup_sync_enabled());
  result.Set("extensionSettingsEnabled",
             sync_service_ && sync_service_->extension_settings_sync_enabled());
  base::ListValue extensions;
  if (sync_service_) {
    for (const auto& [id, restore] : sync_service_->extension_setup_results()) {
      base::DictValue item;
      item.Set("id", id);
      item.Set("status", ExtensionResultLabel(restore.disposition));
      item.Set("canRetry", status.enabled &&
                               sync_service_->extension_setup_sync_enabled() &&
                               (restore.disposition ==
                                    sync::ExtensionRestoreDisposition::kNeedsConfirmation ||
                                restore.disposition ==
                                    sync::ExtensionRestoreDisposition::kFailed ||
                                restore.disposition ==
                                    sync::ExtensionRestoreDisposition::kCancelled));
      item.Set("needsConfirmation",
               restore.disposition ==
                   sync::ExtensionRestoreDisposition::kNeedsConfirmation);
      extensions.Append(std::move(item));
    }
  }
  result.Set("extensionResults", std::move(extensions));
  return result;
}

void AhoiSettingsHandler::ResolveSyncControlsStatus(
    base::Value callback_id,
    std::string_view action) {
  ResolveJavascriptCallback(callback_id,
                            base::Value(BuildSyncControlsStatus(action)));
}

void AhoiSettingsHandler::PushSyncControlsStatus() {
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  FireWebUIListener("ahoi-sync-controls-status-changed",
                    base::Value(BuildSyncControlsStatus({})));
}

void AhoiSettingsHandler::HandleGetSyncControlsStatus(
    const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args) ||
      args.front().GetString().empty() || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  ResolveSyncControlsStatus(args.front().Clone(), {});
}

void AhoiSettingsHandler::HandleSyncControlAction(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || args.front().GetString().empty() ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  if (args.size() < 2u || args.size() > 3u || !args[1].is_string()) {
    ResolveSyncControlsStatus(args.front().Clone(), "blocked");
    return;
  }
  const std::string& action = args[1].GetString();
  const sync::SyncTransportStatus status =
      sync_service_ ? sync_service_->transport_status()
                    : sync::SyncTransportStatus();
  bool accepted = false;
  if (sync_service_ && action == "syncNow" && args.size() == 2u &&
      status.enabled && status.provider_available &&
      status.key_setup_issue.empty() && !status.account_transition_pending &&
      !status.zone_recovery_pending) {
    sync_service_->SyncNowFromUser();
    accepted = true;
  } else if (sync_service_ && action == "retryKey" &&
             args.size() == 2u && status.enabled &&
             !status.key_setup_issue.empty() &&
             !status.account_transition_pending &&
             !status.zone_recovery_pending) {
    sync_service_->RetrySyncKeySetup();
    accepted = true;
  } else if (sync_service_ && action == "extensionSetup" &&
             args.size() == 3u && args[2].is_bool()) {
    accepted = sync_service_->SetExtensionSetupSyncEnabled(args[2].GetBool());
  } else if (sync_service_ && action == "extensionSettings" &&
             args.size() == 3u && args[2].is_bool()) {
    accepted = sync_service_->SetExtensionSettingsSyncEnabled(args[2].GetBool());
  } else if (sync_service_ && action == "retryExtension" &&
             args.size() == 3u && args[2].is_string()) {
    accepted = sync_service_->RetryExtensionSetup(args[2].GetString());
  } else if (sync_service_ && action == "confirmAccount" &&
             args.size() == 3u && args[2].is_bool() &&
             status.account_transition_pending) {
    sync_service_->ConfirmCloudKitAccountTransition(args[2].GetBool());
    accepted = true;
  } else if (sync_service_ && action == "recoverZone" &&
             args.size() == 2u && status.zone_recovery_pending) {
    sync_service_->ConfirmCloudKitZoneRecovery();
    accepted = true;
  }
  ResolveSyncControlsStatus(args.front().Clone(),
                            accepted ? "requested" : "blocked");
}

base::DictValue AhoiSettingsHandler::BuildRemoteControlStatus(
    std::string_view action) const {
  base::DictValue result;
  result.Set("action", std::string(action));
  // Bundle configuration is independent of a stopped transport. Reading it
  // does not create a provider, query an account or activate CloudKit.
  const auto configuration =
      sync::CloudKitSyncConfigurationMac::FromMainBundle();
  const bool configured = configuration && configuration->IsE2EKeyConfigured();
  result.Set("cloudKitAvailable", configured);
  result.Set("syncStatusLabel",
             configured && (!sync_service_ || !sync_service_->sync_enabled())
                 ? (base::i18n::GetConfiguredLocale().starts_with("de")
                        ? "Sync ist ausgeschaltet"
                        : "Sync is off")
                 : "");
  if (!sync_service_) {
    result.Set("prerequisite", "transportUnavailable");
    result.Set("syncEnabled", false);
    result.Set("canPair", false);
    result.Set("canEnable", false);
    result.Set("enabled", false);
    result.Set("approvedDeviceIds", base::ListValue());
    return result;
  }

  const auto prerequisite = sync_service_->remote_control_prerequisite();
  result.Set("prerequisite", PrerequisiteName(prerequisite));
  result.Set("syncEnabled", sync_service_->sync_enabled());
  result.Set("canPair", sync_service_->can_pair_remote_control_device());
  result.Set("canEnable",
             prerequisite ==
                 sync::ProfileSyncService::RemoteControlPrerequisite::kReady);
  result.Set("enabled", sync_service_->remote_control_enabled());
  base::ListValue approved_devices;
  for (const base::Uuid& device_id :
       sync_service_->approved_remote_control_devices()) {
    approved_devices.Append(device_id.AsLowercaseString());
  }
  result.Set("approvedDeviceIds", std::move(approved_devices));
  return result;
}

void AhoiSettingsHandler::ResolveStatus(base::Value callback_id,
                                        std::string_view action) {
  ResolveJavascriptCallback(callback_id,
                            base::Value(BuildRemoteControlStatus(action)));
}

void AhoiSettingsHandler::PushStatus(std::string_view action) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("ahoi-remote-control-status-changed",
                    base::Value(BuildRemoteControlStatus(action)));
}

void AhoiSettingsHandler::HandleGetRemoteControlStatus(
    const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args)) {
    return;
  }
  AllowJavascript();
  ResolveStatus(args.front().Clone(), {});
}

void AhoiSettingsHandler::HandleSetRemoteControlEnabled(
    const base::ListValue& args) {
  if (args.size() != 2u || !HasCallbackId(args) || !args[1].is_bool()) {
    return;
  }
  AllowJavascript();
  const bool enabled = args[1].GetBool();
  const bool changed =
      sync_service_ && sync_service_->SetRemoteControlEnabled(enabled);
  ResolveStatus(args.front().Clone(),
                changed ? (enabled ? "enabled" : "disabled") : "blocked");
}

void AhoiSettingsHandler::HandleApproveRemoteControlDevice(
    const base::ListValue& args) {
  if (args.size() != 3u || !HasCallbackId(args) || !args[1].is_string() ||
      !args[2].is_string()) {
    return;
  }
  AllowJavascript();
  const base::Uuid device_id =
      base::Uuid::ParseCaseInsensitive(args[1].GetString());
  std::string public_key = args[2].GetString();
  base::TrimWhitespaceASCII(public_key, base::TRIM_ALL, &public_key);
  const bool approved = sync_service_ && device_id.is_valid() &&
                        sync_service_->ApproveRemoteControlDevice(
                            device_id, std::move(public_key));
  ResolveStatus(args.front().Clone(),
                approved ? "approved" : "invalidApproval");
}

void AhoiSettingsHandler::HandleRevokeRemoteControlDevice(
    const base::ListValue& args) {
  if (args.size() != 2u || !HasCallbackId(args) || !args[1].is_string()) {
    return;
  }
  AllowJavascript();
  const base::Uuid device_id =
      base::Uuid::ParseCaseInsensitive(args[1].GetString());
  if (!sync_service_ || !device_id.is_valid()) {
    ResolveStatus(args.front().Clone(), "invalidRevocation");
    return;
  }
  sync_service_->RevokeRemoteControlDevice(device_id);
  ResolveStatus(args.front().Clone(), "revoked");
}

}  // namespace ahoi::settings
