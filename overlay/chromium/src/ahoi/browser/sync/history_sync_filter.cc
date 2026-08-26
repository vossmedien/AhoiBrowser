// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/history_sync_filter.h"

namespace ahoi::sync {

bool IsSafeHistoryUrlForSync(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty() &&
         !url.has_username() && !url.has_password();
}

bool ShouldSyncHistoryVisit(const HistorySyncCandidate& candidate) {
  return !candidate.hidden && !candidate.response_is_404 &&
         candidate.source_is_browsed && IsSafeHistoryUrlForSync(candidate.url);
}

}  // namespace ahoi::sync
