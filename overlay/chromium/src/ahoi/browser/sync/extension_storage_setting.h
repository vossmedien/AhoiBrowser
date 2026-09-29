// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_EXTENSION_STORAGE_SETTING_H_
#define AHOI_BROWSER_SYNC_EXTENSION_STORAGE_SETTING_H_

#include "ahoi/browser/sync/extension_storage_types.h"
#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// Existing format3 PermittedSetting, atomic value_json true/false/null. The
// namespace and allowed native key are code-owned; null means a keyed reset.
std::string ExtensionStorageSettingId(std::string_view extension_id,
                                      std::string_view key);
bool IsExtensionStorageSettingId(std::string_view setting_id);
std::optional<ExtensionStorageValue> DecodeExtensionStorageSetting(
    const PermittedSettingRecord& record);
std::optional<PermittedSettingRecord> EncodeExtensionStorageSetting(
    const ExtensionStorageValue& desired,
    SyncVersion version);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_EXTENSION_STORAGE_SETTING_H_
