// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_INTERNAL_H_
#define AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_INTERNAL_H_

#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_model.h"
#include "base/values.h"

namespace ahoi::sync::serialization_internal {

using Dict = base::DictValue;

// Shared wire primitives. Keep existing entities' timestamp, clock and JSON
// semantics here so new domains cannot diverge from the established wire
// format.
void SetTime(Dict& dict, const char* key, base::Time value);
bool ReadTime(const Dict& dict, const char* key, base::Time* value);
void SetCommon(Dict& dict,
               int model_version,
               const base::Uuid& id,
               bool tombstone,
               const SyncVersion& version,
               const FieldVersionMap& field_versions);
bool ReadCommon(const Dict& dict,
                int* model_version,
                base::Uuid* id,
                bool* tombstone,
                SyncVersion* version,
                FieldVersionMap* field_versions);

// Legacy optional UUID handling is retained for existing wire entities.
// Strict optional fields must check presence/type before invoking this helper.
bool ReadUuid(const Dict& dict,
              const char* key,
              base::Uuid* value,
              bool optional);
bool ReadString(const Dict& dict, const char* key, std::string* value);
bool WriteDict(const Dict& dict, std::string* payload);
std::optional<Dict> ParseDict(const std::string& payload);

}  // namespace ahoi::sync::serialization_internal

#endif  // AHOI_BROWSER_SYNC_SYNC_SERIALIZATION_INTERNAL_H_
