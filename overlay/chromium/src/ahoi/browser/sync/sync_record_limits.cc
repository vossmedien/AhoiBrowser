// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_record_limits.h"

#include "base/check.h"
#include "base/strings/string_util.h"

namespace ahoi::sync {

std::string FitSyncText(std::string_view text, size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return std::string(text);
  }
  // Cuts before the first character that no longer fits, never inside one.
  return std::string(base::TruncateUTF8ToByteSize(text, max_bytes));
}

bool FitsSyncHistoryUrl(std::string_view url) {
  return url.size() <= kMaxSyncHistoryUrlBytes;
}

void FitDeviceRecordForSync(DeviceRecord* record) {
  CHECK(record);
  record->display_name =
      FitSyncText(record->display_name, kMaxSyncDeviceNameBytes);
}

void FitWorkspaceRecordForSync(WorkspaceRecord* record) {
  CHECK(record);
  record->name = FitSyncText(record->name, kMaxSyncWorkspaceNameBytes);
}

void FitRemoteTabRecordForSync(RemoteTabRecord* record) {
  CHECK(record);
  record->title = FitSyncText(record->title, kMaxSyncTitleBytes);
}

bool FitHistoryRecordForSync(HistoryRecord* record) {
  CHECK(record);
  if (!FitsSyncHistoryUrl(record->url)) {
    return false;
  }
  record->title = FitSyncText(record->title, kMaxSyncTitleBytes);
  record->transition =
      FitSyncText(record->transition, kMaxSyncHistoryTransitionBytes);
  return true;
}

}  // namespace ahoi::sync
