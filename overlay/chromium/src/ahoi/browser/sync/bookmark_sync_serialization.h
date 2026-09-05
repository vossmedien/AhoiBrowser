// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_BOOKMARK_SYNC_SERIALIZATION_H_
#define AHOI_BROWSER_SYNC_BOOKMARK_SYNC_SERIALIZATION_H_

#include <string>

#include "ahoi/browser/sync/sync_model.h"
#include "base/values.h"

namespace ahoi::sync::serialization_internal {

// Bookmark wire-v2 codec. The public SyncRecord dispatcher owns field-clock
// normalization/completeness checks; this codec owns bookmark field types and
// the exclusive root/parent location representation.
bool SerializeBookmark(const BookmarkRecord& record, std::string* payload);
bool DeserializeBookmark(const base::DictValue& dict, BookmarkRecord* record);

}  // namespace ahoi::sync::serialization_internal

#endif  // AHOI_BROWSER_SYNC_BOOKMARK_SYNC_SERIALIZATION_H_
