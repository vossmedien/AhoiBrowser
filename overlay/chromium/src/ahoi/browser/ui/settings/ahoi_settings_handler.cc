// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"

#include <string>
#include <utility>

#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"

namespace ahoi::settings {
namespace {

const char* PrerequisiteName(
    sync::ProfileSyncService::RemoteControlPrerequisite prerequisite) {
  using Prerequisite =
      sync::ProfileSyncService::RemoteControlPrerequisite;
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

}  // namespace

AhoiSettingsHandler::AhoiSettingsHandler(Profile* profile)
    : profile_(profile),
      sync_service_(
          sync::ProfileSyncServiceFactory::GetForProfile(profile_)) {}

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
      "ahoiGetRemoteControlStatus",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleGetRemoteControlStatus,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiSetRemoteControlEnabled",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleSetRemoteControlEnabled,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiApproveRemoteControlDevice",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleApproveRemoteControlDevice,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiRevokeRemoteControlDevice",
      base::BindRepeating(
          &AhoiSettingsHandler::HandleRevokeRemoteControlDevice,
          base::Unretained(this)));
}

void AhoiSettingsHandler::OnAhoiDeviceTabsChanged(
    const sync::DeviceTabsSnapshot& /*snapshot*/) {}

void AhoiSettingsHandler::OnAhoiSyncStatusChanged(
    const sync::SyncTransportStatus& /*status*/) {
  PushStatus({});
}

base::DictValue AhoiSettingsHandler::BuildRemoteControlStatus(
    std::string_view action) const {
  base::DictValue result;
  result.Set("action", std::string(action));
  if (!sync_service_) {
    result.Set("prerequisite", "transportUnavailable");
    result.Set("syncEnabled", false);
    result.Set("cloudKitAvailable", false);
    result.Set("canPair", false);
    result.Set("canEnable", false);
    result.Set("enabled", false);
    result.Set("approvedDeviceIds", base::ListValue());
    return result;
  }

  const auto prerequisite = sync_service_->remote_control_prerequisite();
  result.Set("prerequisite", PrerequisiteName(prerequisite));
  result.Set("syncEnabled", sync_service_->sync_enabled());
  result.Set("cloudKitAvailable",
             sync_service_->transport_status().provider_available);
  result.Set("canPair", sync_service_->can_pair_remote_control_device());
  result.Set("canEnable",
             prerequisite == sync::ProfileSyncService::
                                 RemoteControlPrerequisite::kReady);
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
  ResolveJavascriptCallback(
      callback_id,
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
