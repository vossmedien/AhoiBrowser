// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BROWSER_SETTING_CATALOG_H_
#define AHOI_BROWSER_SYNC_BROWSER_SETTING_CATALOG_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/values.h"

namespace ahoi::sync {

// Positive, value-specific subset of portable user settings. Membership is not
// consent, a policy override, or a claim to cover every chrome://settings page.
// `category` is a local grouping key; `value_kind` is the native registered
// type.
struct BrowserSettingDescriptor {
  std::string_view id;
  std::string_view category;
  base::Value::Type value_kind;
  bool requires_syncable_flag;
};

base::span<const BrowserSettingDescriptor> GetBrowserSettingCatalog();

// Returns a pointer into the immutable catalog, or nullptr for an excluded ID.
const BrowserSettingDescriptor* FindBrowserSetting(std::string_view id);

// Null explicitly means reset-to-default for a KNOWN catalog entry, not a
// nullable setting, opt-out, deletion, or permission to enumerate other prefs.
// Booleans are strict. Numeric JSON values may use int or double storage;
// INTEGER settings additionally require an exact integral value. After this
// validation, the native adapter must convert numbers to `value_kind` before
// PrefService::Set, and use ClearPref for null. This module touches no
// PrefService.
bool ValidateBrowserSettingValue(std::string_view id, const base::Value& value);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BROWSER_SETTING_CATALOG_H_
