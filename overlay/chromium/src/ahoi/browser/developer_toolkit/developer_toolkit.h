// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_H_

#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_toolkit_browsing_data.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_content_settings.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_document_actions.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"

namespace content {
class WebContents;
}

namespace ahoi {

// Small, on-demand facade for browser UI commands. It has no idle observers,
// timers, or document hooks; a caller supplies a target for every action.
class DeveloperToolkit {
 public:
  DeveloperToolkit(std::unique_ptr<BrowsingDataRemovalAdapter> data_adapter,
                   std::unique_ptr<ContentSettingsAdapter> settings_adapter);
  DeveloperToolkit(const DeveloperToolkit&) = delete;
  DeveloperToolkit& operator=(const DeveloperToolkit&) = delete;
  ~DeveloperToolkit();

  bool ClearCache(const content::WebContents* web_contents,
                  BrowsingDataClearCallback callback);
  bool ClearSiteData(const content::WebContents* web_contents,
                     BrowsingDataClearCallback callback);
  bool ClearBrowsingData(const content::WebContents* web_contents,
                         BrowsingDataClearOptions options,
                         BrowsingDataClearCallback callback);

  std::optional<ContentSettingToggle> ToggleJavaScript(
      const content::WebContents* web_contents);
  std::optional<ContentSettingToggle> ToggleImages(
      const content::WebContents* web_contents);
  std::optional<ContentSettingValue> GetContentSetting(
      const content::WebContents* web_contents,
      ContentSettingType type) const;
  bool ResetContentSettings(const content::WebContents* web_contents);

  static DocumentActionScript GetDocumentAction(DocumentAction action);
  static bool IsSupportedTarget(const content::WebContents* web_contents);

 private:
  BrowsingDataController browsing_data_controller_;
  ContentSettingsController content_settings_controller_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_H_
