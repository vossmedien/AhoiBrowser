// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_POLICY_H_
#define AHOI_BROWSER_SYNC_SYNC_POLICY_H_

#include "base/time/time.h"

namespace ahoi::sync {

// AhoiBrowser deliberately does not expose Chrome account sync. Remote tabs
// and product data are supplied by Ahoi's provider-independent SyncProvider
// (CloudKit in v1), so Google OAuth credentials are neither embedded nor
// required by distributable builds.
constexpr bool IsChromeAccountSyncSupported() {
  return false;
}

inline constexpr base::TimeDelta kTombstoneRetention = base::Days(30);
inline constexpr base::TimeDelta kSessionHeartbeatInterval = base::Minutes(5);
inline constexpr base::TimeDelta kRemoteSessionActionableAge =
    base::Minutes(15);
inline constexpr base::TimeDelta kRemoteSessionVisibleAge = base::Days(7);
inline constexpr int kDefaultHistoryRetentionDays = 90;

constexpr bool IsValidHistoryRetentionDays(int days) {
  return days == -1 || days == 30 || days == 90 || days == 365;
}

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_POLICY_H_
