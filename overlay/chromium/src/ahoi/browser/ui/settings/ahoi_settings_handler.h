// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_
#define AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_

#include <string>
#include <string_view>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/shell_dialogs/select_file_dialog.h"

class Profile;

namespace ahoi::settings {

// Owns explicit Sync category consent, the security-sensitive remote-control
// bridge and a separately selected local workspace export. The WebUI never
// receives approved public-key material or raw file bytes.
class AhoiSettingsHandler final : public content::WebUIMessageHandler,
                                  public sync::ProfileSyncService::Observer,
                                  public ui::SelectFileDialog::Listener {
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

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  bool IsAuthorizedSettingsPage();
  base::DictValue BuildBrowserSettingsSyncStatus(std::string_view action) const;
  void ResolveBrowserSettingsSyncStatus(base::Value callback_id,
                                        std::string_view action);
  void PushBrowserSettingsSyncStatus();
  void HandleGetBrowserSettingsSyncStatus(const base::ListValue& args);
  void HandleSetBrowserSettingsSyncEnabled(const base::ListValue& args);
  base::DictValue BuildSyncControlsStatus(std::string_view action) const;
  void ResolveSyncControlsStatus(base::Value callback_id,
                                 std::string_view action);
  void PushSyncControlsStatus();
  void HandleGetSyncControlsStatus(const base::ListValue& args);
  void HandleSyncControlAction(const base::ListValue& args);
  base::DictValue BuildRemoteControlStatus(std::string_view action) const;
  void ResolveStatus(base::Value callback_id, std::string_view action);
  void PushStatus(std::string_view action);
  void HandleGetRemoteControlStatus(const base::ListValue& args);
  void HandleSetRemoteControlEnabled(const base::ListValue& args);
  void HandleApproveRemoteControlDevice(const base::ListValue& args);
  void HandleRevokeRemoteControlDevice(const base::ListValue& args);
  void HandleGetPortableExportOptions(const base::ListValue& args);
  void HandlePreparePortableExport(const base::ListValue& args);
  void HandleSavePortableExport(const base::ListValue& args);
  void OnPortableExportWritten(bool success);

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<sync::ProfileSyncService> sync_service_ = nullptr;
  base::CallbackListSubscription bookmark_status_subscription_;
  bool observing_sync_service_ = false;
  scoped_refptr<ui::SelectFileDialog> portable_export_dialog_;
  std::string portable_export_token_;
  std::string portable_export_json_;
  bool portable_export_writing_ = false;
  base::WeakPtrFactory<AhoiSettingsHandler> weak_factory_{this};
};

}  // namespace ahoi::settings

#endif  // AHOI_BROWSER_UI_SETTINGS_AHOI_SETTINGS_HANDLER_H_
