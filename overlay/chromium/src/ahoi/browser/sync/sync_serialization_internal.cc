// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_serialization_internal.h"

#include <cmath>
#include <limits>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"

namespace ahoi::sync::serialization_internal {

std::optional<int> ReadInt32(const base::Value& value) {
  if (value.is_int()) {
    return value.GetInt();
  }
  if (!value.is_double()) {
    return std::nullopt;
  }
  const double number = value.GetDouble();
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < std::numeric_limits<int>::min() ||
      number > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(number);
}

std::optional<int> ReadInt32(const Dict& dict, const char* key) {
  const auto* value = dict.Find(key);
  return value ? ReadInt32(*value) : std::nullopt;
}

void SetTime(Dict& dict, const char* key, base::Time value) {
  dict.Set(key, base::NumberToString(
                    value.ToDeltaSinceWindowsEpoch().InMicroseconds()));
}

bool ReadTime(const Dict& dict, const char* key, base::Time* value) {
  const std::string* serialized = dict.FindString(key);
  int64_t micros = 0;
  if (!serialized || !base::StringToInt64(*serialized, &micros) ||
      base::NumberToString(micros) != *serialized) {
    return false;
  }
  *value = base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
  return true;
}

void SetUInt32(Dict& dict, const char* key, uint32_t value) {
  if (value <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    dict.Set(key, static_cast<int>(value));
  } else {
    // All uint32 values are exact in binary64. The canonical writer omits
    // unnecessary decimal suffixes, so Swift receives the same integer bytes.
    dict.Set(key, static_cast<double>(value));
  }
}

bool ReadUInt32(const Dict& dict, const char* key, uint32_t* result) {
  const base::Value* value = dict.Find(key);
  if (!result || !value || (!value->is_int() && !value->is_double())) {
    return false;
  }
  const double number = value->is_int() ? value->GetInt() : value->GetDouble();
  if (!std::isfinite(number) || number < 0 ||
      number > std::numeric_limits<uint32_t>::max() ||
      std::trunc(number) != number) {
    return false;
  }
  *result = static_cast<uint32_t>(number);
  return true;
}

namespace {

void SetVersion(Dict& dict, const SyncVersion& version) {
  dict.Set("version_model", version.model_version);
  dict.Set("version_physical",
           base::NumberToString(version.stamp.physical_time_us));
  SetUInt32(dict, "version_logical", version.stamp.logical);
  dict.Set("version_device", version.stamp.device_tiebreak);
}

bool ReadVersion(const Dict& dict, SyncVersion* version) {
  const std::string* physical = dict.FindString("version_physical");
  const std::string* device = dict.FindString("version_device");
  const std::optional<int> model = ReadInt32(dict, "version_model");
  uint32_t logical = 0;
  int64_t physical_us = 0;
  if (!physical || !device || model != kCurrentModelVersion ||
      !ReadUInt32(dict, "version_logical", &logical) ||
      !base::StringToInt64(*physical, &physical_us) ||
      base::NumberToString(physical_us) != *physical ||
      physical_us < kMinimumSyncClockPhysicalUs ||
      !IsCanonicalSyncDeviceId(*device)) {
    return false;
  }
  version->model_version = *model;
  version->stamp = HlcStamp{.physical_time_us = physical_us,
                            .logical = logical,
                            .device_tiebreak = *device};
  return true;
}

void SetFieldVersions(Dict& dict, const FieldVersionMap& versions) {
  Dict fields;
  for (const auto& [name, stamp] : versions) {
    Dict value;
    value.Set("physical", base::NumberToString(stamp.physical_time_us));
    SetUInt32(value, "logical", stamp.logical);
    value.Set("device", stamp.device_tiebreak);
    fields.Set(name, std::move(value));
  }
  dict.Set("field_versions", std::move(fields));
}

bool ReadFieldVersions(const Dict& dict,
                       int model_version,
                       FieldVersionMap* versions) {
  const Dict* fields = dict.FindDict("field_versions");
  if (!fields) {
    return false;
  }
  for (const auto [name, value] : *fields) {
    if (!value.is_dict()) {
      return false;
    }
    const Dict& stamp = value.GetDict();
    const std::string* physical = stamp.FindString("physical");
    uint32_t logical = 0;
    const std::string* device = stamp.FindString("device");
    int64_t physical_us = 0;
    if (stamp.size() != 3 || !physical || !device ||
        !ReadUInt32(stamp, "logical", &logical) ||
        !base::StringToInt64(*physical, &physical_us) ||
        base::NumberToString(physical_us) != *physical ||
        physical_us < kMinimumSyncClockPhysicalUs ||
        !IsCanonicalSyncDeviceId(*device) ||
        !versions
             ->try_emplace(std::string(name),
                           HlcStamp{.physical_time_us = physical_us,
                                    .logical = logical,
                                    .device_tiebreak = *device})
             .second) {
      return false;
    }
  }
  return model_version == kCurrentModelVersion && !versions->empty();
}

}  // namespace

void SetCommon(Dict& dict,
               int model_version,
               const base::Uuid& id,
               bool tombstone,
               const SyncVersion& version,
               const FieldVersionMap& field_versions) {
  dict.Set("model_version", model_version);
  dict.Set("id", id.AsLowercaseString());
  dict.Set("tombstone", tombstone);
  SetVersion(dict, version);
  SetFieldVersions(dict, field_versions);
}

bool ReadCommon(const Dict& dict,
                int* model_version,
                base::Uuid* id,
                bool* tombstone,
                SyncVersion* version,
                FieldVersionMap* field_versions) {
  const std::optional<int> model = ReadInt32(dict, "model_version");
  const std::string* serialized_id = dict.FindString("id");
  const std::optional<bool> deleted = dict.FindBool("tombstone");
  if (model != kCurrentModelVersion || !serialized_id || !deleted ||
      !IsCanonicalSyncDeviceId(*serialized_id) || !ReadVersion(dict, version)) {
    return false;
  }
  *model_version = *model;
  *id = base::Uuid::ParseLowercase(*serialized_id);
  *tombstone = *deleted;
  return *model == version->model_version &&
         ReadFieldVersions(dict, *model, field_versions);
}

bool ReadUuid(const Dict& dict,
              const char* key,
              base::Uuid* value,
              bool optional) {
  const std::string* serialized = dict.FindString(key);
  if (!serialized) {
    return optional && !dict.contains(key);
  }
  *value = base::Uuid::ParseLowercase(*serialized);
  return value->is_valid() && IsCanonicalSyncDeviceId(*serialized);
}

bool ReadString(const Dict& dict, const char* key, std::string* value) {
  const std::string* serialized = dict.FindString(key);
  if (!serialized) {
    return false;
  }
  *value = *serialized;
  return true;
}

bool WriteDict(const Dict& dict, std::string* payload) {
  return base::JSONWriter::WriteWithOptions(
      base::Value(dict.Clone()),
      base::JSONWriter::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION, payload);
}

std::optional<Dict> ParseDict(const std::string& payload) {
  return base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
}

}  // namespace ahoi::sync::serialization_internal
