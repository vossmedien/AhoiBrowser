// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_HISTORY_SYNC_FILTER_H_
#define AHOI_BROWSER_SYNC_HISTORY_SYNC_FILTER_H_

#include "url/gurl.h"

namespace ahoi::sync {

struct HistorySyncCandidate {
  GURL url;
  bool hidden = false;
  bool response_is_404 = false;
  bool source_is_browsed = true;
};

// This allowlist is shared by visit observation and deletion export. It never
// accepts credentials or non-network schemes and deliberately excludes visits
// inserted by sync so reconciliation cannot loop back into the outbox.
bool ShouldSyncHistoryVisit(const HistorySyncCandidate& candidate);
bool IsSafeHistoryUrlForSync(const GURL& url);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_HISTORY_SYNC_FILTER_H_
