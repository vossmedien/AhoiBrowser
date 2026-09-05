// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_serialization_internal.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"

namespace ahoi::sync::serialization_internal {

void SetTime(Dict& dict, const char* key, base::Time value) {
  dict.Set(key, base::NumberToString(
                    value.ToDeltaSinceWindowsEpoch().InMicroseconds()));
}

bool ReadTime(const Dict& dict, const char* key, base::Time* value) {
  const std::string* serialized = dict.FindString(key);
  int64_t micros = 0;
  if (!serialized || !base::StringToInt64(*serialized, &micros)) {
    return false;
  }
  *value = base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
  return true;
}

namespace {

void SetVersion(Dict& dict, const SyncVersion& version) {
  dict.Set("version_model", version.model_version);
  dict.Set("version_physical",
           base::NumberToString(version.stamp.physical_time_us));
  dict.Set("version_logical", static_cast<int>(version.stamp.logical));
  dict.Set("version_device", version.stamp.device_tiebreak);
}

bool ReadVersion(const Dict& dict, SyncVersion* version) {
  const std::string* physical = dict.FindString("version_physical");
  const std::string* device = dict.FindString("version_device");
  const std::optional<int> model = dict.FindInt("version_model");
  const std::optional<int> logical = dict.FindInt("version_logical");
  int64_t physical_us = 0;
  if (!physical || !device || device->empty() || !model || !logical ||
      *logical < 0 || !base::StringToInt64(*physical, &physical_us)) {
    return false;
  }
  version->model_version = *model;
  version->stamp = HlcStamp{.physical_time_us = physical_us,
                            .logical = static_cast<uint32_t>(*logical),
                            .device_tiebreak = *device};
  return true;
}

void SetFieldVersions(Dict& dict,
                      int model_version,
                      const FieldVersionMap& versions) {
  if (model_version < 2) {
    return;
  }
  Dict fields;
  for (const auto& [name, stamp] : versions) {
    Dict value;
    value.Set("physical", base::NumberToString(stamp.physical_time_us));
    value.Set("logical", static_cast<int>(stamp.logical));
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
    return model_version < 2;
  }
  for (const auto [name, value] : *fields) {
    if (!value.is_dict()) {
      return false;
    }
    const Dict& stamp = value.GetDict();
    const std::string* physical = stamp.FindString("physical");
    const std::optional<int> logical = stamp.FindInt("logical");
    const std::string* device = stamp.FindString("device");
    int64_t physical_us = 0;
    if (!physical || !logical || *logical < 0 || !device || device->empty() ||
        !base::StringToInt64(*physical, &physical_us) || physical_us < 0 ||
        !versions
             ->try_emplace(std::string(name),
                           HlcStamp{.physical_time_us = physical_us,
                                    .logical = static_cast<uint32_t>(*logical),
                                    .device_tiebreak = *device})
             .second) {
      return false;
    }
  }
  return model_version < 2 || !versions->empty();
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
  SetFieldVersions(dict, model_version, field_versions);
}

bool ReadCommon(const Dict& dict,
                int* model_version,
                base::Uuid* id,
                bool* tombstone,
                SyncVersion* version,
                FieldVersionMap* field_versions) {
  const std::optional<int> model = dict.FindInt("model_version");
  const std::string* serialized_id = dict.FindString("id");
  const std::optional<bool> deleted = dict.FindBool("tombstone");
  if (!model || !serialized_id || !deleted ||
      !base::Uuid::ParseLowercase(*serialized_id).is_valid() ||
      !ReadVersion(dict, version)) {
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
    return optional;
  }
  *value = base::Uuid::ParseLowercase(*serialized);
  return value->is_valid();
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
  return base::JSONWriter::Write(base::Value(dict.Clone()), payload);
}

std::optional<Dict> ParseDict(const std::string& payload) {
  return base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
}

}  // namespace ahoi::sync::serialization_internal
