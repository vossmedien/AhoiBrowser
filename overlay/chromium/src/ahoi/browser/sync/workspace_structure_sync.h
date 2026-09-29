// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AHOI_BROWSER_SYNC_WORKSPACE_STRUCTURE_SYNC_H_
#define AHOI_BROWSER_SYNC_WORKSPACE_STRUCTURE_SYNC_H_
#include "ahoi/browser/sync/sync_model.h"
#include "base/values.h"
namespace ahoi::sync {
bool ValidateSplitMetadata(const SharedSplitMetadata& value);
bool ValidateArchiveSnapshot(const SharedArchiveSnapshot& value);
base::Uuid ArchiveIdForSnapshot(const SharedArchiveSnapshot& value);
bool ValidateArchiveEntry(const TabArchiveEntryRecord& value);
bool ValidateHomeTarget(const std::optional<SharedTabTarget>& value);
namespace serialization_internal {
void SetHomeTarget(base::DictValue& dict,
                   const std::optional<SharedTabTarget>& target);
bool ReadHomeTarget(const base::DictValue& dict,
                    std::optional<SharedTabTarget>* target);
bool SerializeSplitGroup(const SplitGroupRecord& value, std::string* payload);
bool DeserializeSplitGroup(const base::DictValue& dict,
                           SplitGroupRecord* value);
bool SerializeArchiveEntry(const TabArchiveEntryRecord& value,
                           std::string* payload);
bool DeserializeArchiveEntry(const base::DictValue& dict,
                             TabArchiveEntryRecord* value);
}  // namespace serialization_internal
}  // namespace ahoi::sync
#endif
