// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_H_
#define AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_H_

#include <string>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

// The payload is deliberately canonical JSON rather than a provider-specific
// protobuf. It is stable enough for a CloudKit record, an HTTP relay, and the
// future Swift client to consume without coupling this local store to any one
// transport. Timestamps are encoded as decimal microseconds (strings) so no
// JSON number loses precision.
bool SerializeRecord(const SyncRecord& record, std::string* payload);
bool DeserializeRecord(EntityType expected_type,
                       const std::string& payload,
                       SyncRecord* record);

bool ValidateChangeEnvelope(const SyncChange& change, SyncRecord* decoded);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_H_
