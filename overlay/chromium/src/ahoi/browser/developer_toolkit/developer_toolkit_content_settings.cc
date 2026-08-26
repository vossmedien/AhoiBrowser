// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_content_settings.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

ContentSettingsController::ContentSettingsController(
    std::unique_ptr<ContentSettingsAdapter> adapter)
    : adapter_(std::move(adapter)) {}

ContentSettingsController::~ContentSettingsController() = default;

ContentSettingValue ToggleContentSettingValue(
    std::optional<ContentSettingValue> current) {
  const ContentSettingValue value =
      current.value_or(ContentSettingValue::kAllow);
  return value == ContentSettingValue::kAllow ? ContentSettingValue::kBlock
                                              : ContentSettingValue::kAllow;
}

std::optional<ContentSettingToggle> ContentSettingsController::Toggle(
    const content::WebContents* web_contents,
    ContentSettingType type) {
  std::optional<url::Origin> origin = GetOrigin(web_contents);
  if (!adapter_ || !origin) {
    return std::nullopt;
  }

  const ContentSettingValue next =
      ToggleContentSettingValue(adapter_->Get(*origin, type));
  if (!adapter_->Set(*origin, type, next)) {
    return std::nullopt;
  }

  return ContentSettingToggle{.origin = *origin, .type = type, .value = next};
}

std::optional<ContentSettingValue> ContentSettingsController::Get(
    const content::WebContents* web_contents,
    ContentSettingType type) const {
  std::optional<url::Origin> origin = GetOrigin(web_contents);
  if (!adapter_ || !origin) {
    return std::nullopt;
  }
  return adapter_->Get(*origin, type);
}

bool ContentSettingsController::Reset(const content::WebContents* web_contents,
                                      ContentSettingType type) {
  std::optional<url::Origin> origin = GetOrigin(web_contents);
  return adapter_ && origin && adapter_->Reset(*origin, type);
}

std::optional<url::Origin> ContentSettingsController::GetOrigin(
    const content::WebContents* web_contents) const {
  const GURL target = GetSupportedDeveloperTargetUrl(web_contents);
  if (!target.is_valid()) {
    return std::nullopt;
  }
  return url::Origin::Create(target);
}

}  // namespace ahoi
