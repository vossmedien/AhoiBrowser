// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit.h"

#include <utility>

namespace ahoi {

DeveloperToolkit::DeveloperToolkit(
    std::unique_ptr<BrowsingDataRemovalAdapter> data_adapter,
    std::unique_ptr<ContentSettingsAdapter> settings_adapter)
    : browsing_data_controller_(std::move(data_adapter)),
      content_settings_controller_(std::move(settings_adapter)) {}

DeveloperToolkit::~DeveloperToolkit() = default;

bool DeveloperToolkit::ClearCache(const content::WebContents* web_contents,
                                  BrowsingDataClearCallback callback) {
  return browsing_data_controller_.ClearCache(web_contents,
                                              std::move(callback));
}

bool DeveloperToolkit::ClearSiteData(const content::WebContents* web_contents,
                                     BrowsingDataClearCallback callback) {
  return browsing_data_controller_.ClearSiteData(web_contents,
                                                 std::move(callback));
}

bool DeveloperToolkit::ClearBrowsingData(
    const content::WebContents* web_contents,
    BrowsingDataClearOptions options,
    BrowsingDataClearCallback callback) {
  return browsing_data_controller_.ClearData(web_contents, options,
                                             std::move(callback));
}

std::optional<ContentSettingToggle> DeveloperToolkit::ToggleJavaScript(
    const content::WebContents* web_contents) {
  return content_settings_controller_.Toggle(web_contents,
                                             ContentSettingType::kJavaScript);
}

std::optional<ContentSettingToggle> DeveloperToolkit::ToggleImages(
    const content::WebContents* web_contents) {
  return content_settings_controller_.Toggle(web_contents,
                                             ContentSettingType::kImages);
}

std::optional<ContentSettingValue> DeveloperToolkit::GetContentSetting(
    const content::WebContents* web_contents,
    ContentSettingType type) const {
  return content_settings_controller_.Get(web_contents, type);
}

bool DeveloperToolkit::ResetContentSettings(
    const content::WebContents* web_contents) {
  const bool javascript = content_settings_controller_.Reset(
      web_contents, ContentSettingType::kJavaScript);
  const bool images = content_settings_controller_.Reset(
      web_contents, ContentSettingType::kImages);
  return javascript && images;
}

DocumentActionScript DeveloperToolkit::GetDocumentAction(
    DocumentAction action) {
  return GetDocumentActionScript(action);
}

bool DeveloperToolkit::IsSupportedTarget(
    const content::WebContents* web_contents) {
  return IsSupportedDeveloperTarget(web_contents);
}

}  // namespace ahoi
