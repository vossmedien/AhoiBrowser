// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_EXTENSION_SETUP_SETTING_H_
#define AHOI_BROWSER_SYNC_EXTENSION_SETUP_SETTING_H_

#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/extension_setup_types.h"
#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// Typed setup configuration in the EXISTING format3 PermittedSetting class,
// never the device-specific observed inventory. One atomic value_json group
// prevents enable/source state from outliving its corresponding install state.
std::string ExtensionSetupSettingId(std::string_view extension_id);
bool IsExtensionSetupSettingId(std::string_view setting_id);
std::optional<ExtensionDesiredConfiguration> DecodeExtensionSetupSetting(
    const PermittedSettingRecord& record);
std::optional<PermittedSettingRecord> EncodeExtensionSetupSetting(
    const ExtensionDesiredConfiguration& desired,
    SyncVersion version);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_EXTENSION_SETUP_SETTING_H_
