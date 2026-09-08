// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_FIELD_VALUES_H_
#define AHOI_BROWSER_SYNC_SYNC_FIELD_VALUES_H_

#include <string_view>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync::field_internal {

// Compare/copy one field value or complete atomic value group. These helpers
// neither select a winning clock nor normalize records, versions or targets.
// Unknown fields and different record types compare unequal and copy nothing.
bool FieldEqual(const SyncRecord& left,
                const SyncRecord& right,
                std::string_view field);

// destination must be non-null; its identity and clock metadata stay unchanged.
void CopyField(const SyncRecord& source,
               std::string_view field,
               SyncRecord* destination);

}  // namespace ahoi::sync::field_internal

#endif  // AHOI_BROWSER_SYNC_SYNC_FIELD_VALUES_H_
