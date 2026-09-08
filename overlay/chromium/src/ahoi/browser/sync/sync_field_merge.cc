// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/sync_field_values.h"
#include "ahoi/browser/sync/sync_merge.h"

namespace ahoi::sync {
namespace {

using field_internal::CopyField;
using field_internal::FieldEqual;

using Fields = std::vector<std::string_view>;

void SetError(std::string_view message, std::string* error) {
  if (error) {
    *error = message;
  }
}

Fields FieldNames(EntityType type) {
  switch (type) {
    case EntityType::kDevice:
      return {"type",      "display_name", "created_at",
              "last_seen", "retired",      "tombstone"};
    case EntityType::kWorkspace:
      return {"name",       "icon",        "sort_key", "accent_argb",
              "created_at", "modified_at", "tombstone"};
    case EntityType::kTreeNode:
      return {"location",     "kind",     "title",      "icon",
              "accent_argb",  "url",      "created_at", "modified_at",
              "is_temporary", "tombstone"};
    case EntityType::kHistoryEntry:
      return {"device_id",   "url",        "title",    "last_visit",
              "visit_count", "transition", "tombstone"};
    case EntityType::kRemoteTab:
      return {"device_id",    "session_id",   "workspace_id", "url",
              "title",        "opened_at",    "last_active",  "pinned",
              "is_incognito", "tree_node_id", "tombstone"};
    case EntityType::kDeviceSession:
      return {"device_id", "started_at", "liveness", "tombstone"};
    case EntityType::kRemoteCommand:
      return {"request", "status", "tombstone"};
    case EntityType::kAppearance:
      return {"color_mode", "accent_argb", "use_system_accent", "tombstone"};
    case EntityType::kPermittedSetting:
      return {"setting_id", "value_json", "tombstone"};
    case EntityType::kExtensionInventory:
      return {"device_id",         "extension_id", "name",
              "extension_version", "enabled",      "tombstone"};
    case EntityType::kDeveloperAsset:
      return {"kind",    "name",     "scope",    "source",
              "enabled", "opted_in", "tombstone"};
    case EntityType::kBookmark:
      return {"location", "kind", "title", "url", "created_at", "tombstone"};
    case EntityType::kDeviceCapability:
      return {"device_id", "capabilities", "tombstone"};
  }
  return {};
}

const FieldVersionMap& Versions(const SyncRecord& record) {
  return std::visit(
      [](const auto& value) -> const FieldVersionMap& {
        return value.field_versions;
      },
      record);
}

FieldVersionMap& Versions(SyncRecord* record) {
  return std::visit(
      [](auto& value) -> FieldVersionMap& { return value.field_versions; },
      *record);
}

SyncVersion& MutableVersion(SyncRecord* record) {
  return std::visit([](auto& value) -> SyncVersion& { return value.version; },
                    *record);
}

void SetModelVersion(SyncRecord* record, int version) {
  std::visit([version](auto& value) { value.model_version = version; },
             *record);
  MutableVersion(record).model_version = version;
}

bool IsKnownField(EntityType type, std::string_view field) {
  const Fields fields = FieldNames(type);
  return std::ranges::find(fields, field) != fields.end();
}

bool IsImmutableField(EntityType type, std::string_view field) {
  switch (type) {
    case EntityType::kDevice:
      return field == "type" || field == "created_at";
    case EntityType::kWorkspace:
      return field == "created_at";
    case EntityType::kTreeNode:
      return field == "kind" || field == "created_at";
    case EntityType::kHistoryEntry:
      return field == "device_id" || field == "url" || field == "last_visit" ||
             field == "visit_count" || field == "transition";
    case EntityType::kRemoteTab:
      return field == "device_id" || field == "session_id" ||
             field == "opened_at" || field == "is_incognito";
    case EntityType::kDeviceSession:
      return field == "device_id" || field == "started_at";
    case EntityType::kRemoteCommand:
      return field == "request";
    case EntityType::kAppearance:
      return false;
    case EntityType::kPermittedSetting:
      return field == "setting_id";
    case EntityType::kExtensionInventory:
      return field == "device_id" || field == "extension_id";
    case EntityType::kDeveloperAsset:
      return field == "kind";
    case EntityType::kBookmark:
      return field == "kind";
    case EntityType::kDeviceCapability:
      return field == "device_id";
  }
  return false;
}

HlcStamp FieldStamp(const SyncRecord& record, std::string_view field) {
  const auto found = Versions(record).find(std::string(field));
  return found == Versions(record).end() ? GetVersion(record).stamp
                                         : found->second;
}

bool SameProjectedRecord(const SyncRecord& left, const SyncRecord& right) {
  if (GetEntityType(left) != GetEntityType(right) ||
      GetEntityId(left) != GetEntityId(right)) {
    return false;
  }
  for (std::string_view field : FieldNames(GetEntityType(left))) {
    if (!FieldEqual(left, right, field) ||
        FieldStamp(left, field) != FieldStamp(right, field)) {
      return false;
    }
  }
  return true;
}

bool IsTerminal(RemoteCommandStatus status) {
  return status == RemoteCommandStatus::kExecuted ||
         status == RemoteCommandStatus::kFailed;
}

bool KeepExistingCommandStatus(const SyncRecord& existing,
                               const SyncRecord& incoming,
                               std::string_view field) {
  if (field != "status") {
    return false;
  }
  const auto* old_command = std::get_if<RemoteCommandRecord>(&existing);
  const auto* new_command = std::get_if<RemoteCommandRecord>(&incoming);
  if (!old_command || !new_command) {
    return false;
  }
  return IsTerminal(old_command->status) ||
         static_cast<int>(new_command->status) <
             static_cast<int>(old_command->status);
}

std::optional<HlcStamp> NextMergeStamp(HlcStamp stamp) {
  if (stamp.logical < std::numeric_limits<uint32_t>::max()) {
    ++stamp.logical;
    return stamp;
  }
  if (stamp.physical_time_us == std::numeric_limits<int64_t>::max()) {
    return std::nullopt;
  }
  ++stamp.physical_time_us;
  stamp.logical = 0;
  return stamp;
}

}  // namespace

bool NormalizeFieldVersions(SyncRecord* record, std::string* error) {
  if (!record || GetVersion(*record).model_version != kCurrentModelVersion ||
      !IsValidSyncClock(GetVersion(*record).stamp) ||
      !std::visit(
          [](const auto& value) {
            return value.model_version == kCurrentModelVersion;
          },
          *record)) {
    SetError("current model and valid creation clock required", error);
    return false;
  }
  const EntityType type = GetEntityType(*record);
  FieldVersionMap& versions = Versions(record);
  for (const auto& [field, stamp] : versions) {
    if (!IsKnownField(type, field) || !IsValidSyncClock(stamp) ||
        stamp > GetVersion(*record).stamp) {
      SetError("invalid field version", error);
      return false;
    }
  }
  if (!versions.empty() && versions.size() != FieldNames(type).size()) {
    SetError("incomplete field version map", error);
    return false;
  }
  for (std::string_view field : FieldNames(type)) {
    versions.try_emplace(std::string(field), GetVersion(*record).stamp);
  }
  return true;
}

bool HasCompleteFieldVersions(const SyncRecord& record) {
  if (GetVersion(record).model_version != kCurrentModelVersion ||
      Versions(record).empty()) {
    return false;
  }
  const size_t supplied = Versions(record).size();
  SyncRecord normalized = record;
  return NormalizeFieldVersions(&normalized, nullptr) &&
         supplied == Versions(normalized).size();
}

bool StampLocalMutation(const SyncRecord* existing,
                        SyncRecord* local,
                        std::string* error) {
  if (!local || !NormalizeFieldVersions(local, error)) {
    return false;
  }
  SetModelVersion(local, kCurrentModelVersion);
  if (!existing) {
    return true;
  }

  SyncRecord normalized_existing = *existing;
  if (!HasCompleteFieldVersions(normalized_existing) ||
      GetEntityType(normalized_existing) != GetEntityType(*local) ||
      GetEntityId(normalized_existing) != GetEntityId(*local)) {
    SetError("local mutation identity mismatch", error);
    return false;
  }
  for (std::string_view field : FieldNames(GetEntityType(*local))) {
    if (IsImmutableField(GetEntityType(*local), field) &&
        !FieldEqual(normalized_existing, *local, field)) {
      SetError("local immutable field conflict", error);
      return false;
    }
    Versions(local).insert_or_assign(
        std::string(field), FieldEqual(normalized_existing, *local, field)
                                ? FieldStamp(normalized_existing, field)
                                : GetVersion(*local).stamp);
  }
  return true;
}

MergeDecision MergeRecordFields(const SyncRecord& existing,
                                const SyncRecord& incoming,
                                SyncRecord* merged,
                                std::string* error) {
  if (!merged || GetEntityType(existing) != GetEntityType(incoming) ||
      GetEntityId(existing) != GetEntityId(incoming)) {
    SetError("merge identity mismatch", error);
    return MergeDecision::kInvalid;
  }
  SyncRecord old_value = existing;
  SyncRecord new_value = incoming;
  if (!HasCompleteFieldVersions(old_value) ||
      !HasCompleteFieldVersions(new_value)) {
    SetError("current complete field maps required", error);
    return MergeDecision::kInvalid;
  }

  const EntityType type = GetEntityType(old_value);
  for (std::string_view field : FieldNames(type)) {
    if (IsImmutableField(type, field) &&
        !FieldEqual(old_value, new_value, field)) {
      SetError("immutable field conflict", error);
      return MergeDecision::kInvalid;
    }
  }

  *merged = old_value;
  for (std::string_view field : FieldNames(type)) {
    const HlcStamp old_stamp = FieldStamp(old_value, field);
    const HlcStamp new_stamp = FieldStamp(new_value, field);
    if (new_stamp == old_stamp) {
      if (!FieldEqual(old_value, new_value, field)) {
        SetError("equal field clock conflict", error);
        return MergeDecision::kInvalid;
      }
      continue;
    }
    if (new_stamp > old_stamp &&
        !KeepExistingCommandStatus(old_value, new_value, field)) {
      CopyField(new_value, field, merged);
      Versions(merged).insert_or_assign(std::string(field), new_stamp);
    }
  }

  SetModelVersion(merged, kCurrentModelVersion);
  MutableVersion(merged).stamp =
      std::max(GetVersion(old_value).stamp, GetVersion(new_value).stamp);
  const bool same_as_old = SameProjectedRecord(*merged, old_value);
  const bool same_as_new = SameProjectedRecord(*merged, new_value);
  if (same_as_old && same_as_new) {
    return MergeDecision::kDuplicate;
  }
  if (same_as_old) {
    return MergeDecision::kKeepExisting;
  }
  if (same_as_new) {
    return MergeDecision::kAcceptIncoming;
  }
  const std::optional<HlcStamp> merge_stamp =
      NextMergeStamp(MutableVersion(merged).stamp);
  if (!merge_stamp) {
    SetError("merge clock exhausted", error);
    return MergeDecision::kInvalid;
  }
  MutableVersion(merged).stamp = *merge_stamp;
  return MergeDecision::kMergeFields;
}

}  // namespace ahoi::sync
