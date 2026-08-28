// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_
#define AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_

#include <string_view>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace ahoi::settings {

// Owns the security-sensitive Settings bridge for remote-control pairing and
// activation. The WebUI never receives approved public-key material and never
// writes the receive-policy preference directly.
class AhoiSettingsHandler final
    : public content::WebUIMessageHandler,
      public sync::ProfileSyncService::Observer {
 public:
  explicit AhoiSettingsHandler(Profile* profile);
  AhoiSettingsHandler(const AhoiSettingsHandler&) = delete;
  AhoiSettingsHandler& operator=(const AhoiSettingsHandler&) = delete;
  ~AhoiSettingsHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

  // sync::ProfileSyncService::Observer:
  void OnAhoiDeviceTabsChanged(
      const sync::DeviceTabsSnapshot& snapshot) override;
  void OnAhoiSyncStatusChanged(
      const sync::SyncTransportStatus& status) override;

 private:
  base::DictValue BuildRemoteControlStatus(std::string_view action) const;
  void ResolveStatus(base::Value callback_id, std::string_view action);
  void PushStatus(std::string_view action);
  void HandleGetRemoteControlStatus(const base::ListValue& args);
  void HandleSetRemoteControlEnabled(const base::ListValue& args);
  void HandleApproveRemoteControlDevice(const base::ListValue& args);
  void HandleRevokeRemoteControlDevice(const base::ListValue& args);

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<sync::ProfileSyncService> sync_service_ = nullptr;
  bool observing_sync_service_ = false;
};

}  // namespace ahoi::settings

#endif  // AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_
