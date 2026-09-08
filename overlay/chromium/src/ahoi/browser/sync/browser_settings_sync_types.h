// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BROWSER_SETTINGS_SYNC_TYPES_H_
#define AHOI_BROWSER_SYNC_BROWSER_SETTINGS_SYNC_TYPES_H_

#include <string_view>
#include <vector>

#include "ahoi/browser/sync/sync_authorization.h"
#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

base::Uuid BrowserSettingRecordId(std::string_view setting_id);
bool IsPortableBrowserSetting(const PermittedSettingRecord& record);

// A local, original-scope read lease, not a new wire record or another engine.
// A fresh profile may seed its USER values only after the first cloud fetch.
struct BrowserSettingsProjection {
  std::vector<PermittedSettingRecord> records;
  HlcStamp observed_clock;
  bool initial_fetch_complete = false;
  SyncAuthorization authorization;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BROWSER_SETTINGS_SYNC_TYPES_H_
