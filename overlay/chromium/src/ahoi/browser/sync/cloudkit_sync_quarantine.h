// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_QUARANTINE_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_QUARANTINE_H_

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// Produces a deliberately invalid, payload-free transport envelope. Passing it
// through SyncStore records bounded quarantine metadata while allowing the
// provider page token to advance. No malformed encrypted value is copied.
SyncChange MakeCloudKitQuarantineMarker(EntityType claimed_type);
bool IsCloudKitQuarantineMarker(const SyncChange& change);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_QUARANTINE_H_
