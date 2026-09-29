// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BOOKMARK_NATIVE_OBSERVATION_H_
#define AHOI_BROWSER_SYNC_BOOKMARK_NATIVE_OBSERVATION_H_

#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// Local ledger content only. An observation has no authored field clocks and
// is never a SyncRecord/envelope/outbox payload. Actual apply receipts continue
// to store the complete current-format record they refer to.
bool SerializeNativeBookmarkObservation(const BookmarkRecord& observation,
                                        std::string* payload);
std::optional<BookmarkRecord> DeserializeNativeBookmarkObservation(
    const std::string& payload);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_BOOKMARK_NATIVE_OBSERVATION_H_
