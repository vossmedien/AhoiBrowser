// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_quarantine.h"

#include "base/uuid.h"

namespace ahoi::sync {

SyncChange MakeCloudKitQuarantineMarker(EntityType claimed_type) {
  const base::Uuid id = base::Uuid::GenerateRandomV4();
  return {.mutation_id = "cloud-invalid:" + id.AsLowercaseString(),
          .entity_type = claimed_type,
          .entity_id = id,
          .kind = ChangeKind::kUpsert,
          .version = {.stamp = {.device_tiebreak = "quarantine"}},
          .payload = "{}"};
}

bool IsCloudKitQuarantineMarker(const SyncChange& change) {
  return change.mutation_id.starts_with("cloud-invalid:") &&
         change.entity_id.is_valid() && change.kind == ChangeKind::kUpsert &&
         change.version.model_version == kCurrentModelVersion &&
         change.version.stamp.physical_time_us == 0 &&
         change.version.stamp.logical == 0 &&
         change.version.stamp.device_tiebreak == "quarantine" &&
         change.payload == "{}";
}

}  // namespace ahoi::sync
