// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_RECORD_LIMITS_H_
#define AHOI_BROWSER_SYNC_SYNC_RECORD_LIMITS_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// The strictest reader of a synced record decides what a writer may put into
// it (config/sync-format.json `recordTextLimits`, Crest 77457bb). Today that is
// the Companion: `RemoteTab` and `HistoryVisit` reject longer metadata and the
// bridge quarantines the whole record. A writer therefore fits every record it
// authors; the local browser keeps its own text untouched.
inline constexpr size_t kMaxSyncTitleBytes = 1024;
inline constexpr size_t kMaxSyncDeviceNameBytes = 256;
inline constexpr size_t kMaxSyncWorkspaceNameBytes = 256;
inline constexpr size_t kMaxSyncHistoryUrlBytes = 16 * 1024;
inline constexpr size_t kMaxSyncHistoryTransitionBytes = 128;

// `text` cut after the last whole UTF-8 character that keeps it within
// `max_bytes`. Text that already fits is returned unchanged.
std::string FitSyncText(std::string_view text, size_t max_bytes);

// Whether a history address is short enough for every reader. A longer one
// stays on this device; it is never cut, because a cut address is another page.
bool FitsSyncHistoryUrl(std::string_view url);

// Fit the metadata of a record this device authors. They change only the
// named text fields and never the identity, clock or address of the record.
void FitDeviceRecordForSync(DeviceRecord* record);
void FitWorkspaceRecordForSync(WorkspaceRecord* record);
void FitRemoteTabRecordForSync(RemoteTabRecord* record);
// Returns false, leaving `record` untouched, when its address must stay local.
bool FitHistoryRecordForSync(HistoryRecord* record);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_RECORD_LIMITS_H_
