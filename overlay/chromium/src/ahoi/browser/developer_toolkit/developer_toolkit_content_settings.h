// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTENT_SETTINGS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTENT_SETTINGS_H_

#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"

namespace content {
class WebContents;
}

namespace ahoi {

// The production adapter can wrap HostContentSettingsMap. Keeping it behind
// this narrow origin/value contract lets unit tests use a map and avoids a
// Profile dependency in the toolkit core.
class ContentSettingsAdapter {
 public:
  virtual ~ContentSettingsAdapter() = default;

  virtual std::optional<ContentSettingValue> Get(
      const url::Origin& origin,
      ContentSettingType type) const = 0;
  virtual bool Set(const url::Origin& origin,
                   ContentSettingType type,
                   ContentSettingValue value) = 0;
  virtual bool Reset(const url::Origin& origin, ContentSettingType type) = 0;
};

ContentSettingValue ToggleContentSettingValue(
    std::optional<ContentSettingValue> current);

class ContentSettingsController {
 public:
  explicit ContentSettingsController(
      std::unique_ptr<ContentSettingsAdapter> adapter);
  ContentSettingsController(const ContentSettingsController&) = delete;
  ContentSettingsController& operator=(const ContentSettingsController&) =
      delete;
  ~ContentSettingsController();

  std::optional<ContentSettingToggle> Toggle(
      const content::WebContents* web_contents,
      ContentSettingType type);
  std::optional<ContentSettingValue> Get(
      const content::WebContents* web_contents,
      ContentSettingType type) const;
  bool Reset(const content::WebContents* web_contents, ContentSettingType type);

 private:
  std::optional<url::Origin> GetOrigin(
      const content::WebContents* web_contents) const;

  std::unique_ptr<ContentSettingsAdapter> adapter_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_CONTENT_SETTINGS_H_
