// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/sync_merge.h"

namespace ahoi::sync {
namespace {

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
      return {"location",   "kind",        "title",
              "icon",       "accent_argb", "url",
              "created_at", "modified_at", "tombstone"};
    case EntityType::kHistoryEntry:
      return {"device_id",   "url",        "title",    "last_visit",
              "visit_count", "transition", "tombstone"};
    case EntityType::kRemoteTab:
      return {"device_id",    "session_id", "workspace_id", "url",
              "title",        "opened_at",  "last_active",  "pinned",
              "is_incognito", "tombstone"};
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
  }
  return false;
}

bool FieldEqual(const SyncRecord& left,
                const SyncRecord& right,
                std::string_view field) {
  return std::visit(
      [field](const auto& a, const auto& b) {
        using A = std::decay_t<decltype(a)>;
        using B = std::decay_t<decltype(b)>;
        if constexpr (!std::is_same_v<A, B>) {
          return false;
        } else if constexpr (std::is_same_v<A, DeviceRecord>) {
          if (field == "type") {
            return a.type == b.type;
          }
          if (field == "display_name") {
            return a.display_name == b.display_name;
          }
          if (field == "created_at") {
            return a.created_at == b.created_at;
          }
          if (field == "last_seen") {
            return a.last_seen == b.last_seen;
          }
          if (field == "retired") {
            return a.retired == b.retired;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, WorkspaceRecord>) {
          if (field == "name") {
            return a.name == b.name;
          }
          if (field == "icon") {
            return a.icon == b.icon;
          }
          if (field == "sort_key") {
            return a.sort_key == b.sort_key;
          }
          if (field == "accent_argb") {
            return a.accent_argb == b.accent_argb;
          }
          if (field == "created_at") {
            return a.created_at == b.created_at;
          }
          if (field == "modified_at") {
            return a.modified_at == b.modified_at;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, TreeNodeRecord>) {
          if (field == "location") {
            return a.workspace_id == b.workspace_id &&
                   a.parent_id == b.parent_id && a.sort_key == b.sort_key;
          }
          if (field == "kind") {
            return a.kind == b.kind;
          }
          if (field == "title") {
            return a.title == b.title;
          }
          if (field == "icon") {
            return a.icon == b.icon;
          }
          if (field == "accent_argb") {
            return a.accent_argb == b.accent_argb;
          }
          if (field == "url") {
            return a.url == b.url;
          }
          if (field == "created_at") {
            return a.created_at == b.created_at;
          }
          if (field == "modified_at") {
            return a.modified_at == b.modified_at;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, HistoryRecord>) {
          if (field == "device_id") {
            return a.device_id == b.device_id;
          }
          if (field == "url") {
            return a.url == b.url;
          }
          if (field == "title") {
            return a.title == b.title;
          }
          if (field == "last_visit") {
            return a.last_visit == b.last_visit;
          }
          if (field == "visit_count") {
            return a.visit_count == b.visit_count;
          }
          if (field == "transition") {
            return a.transition == b.transition;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, RemoteTabRecord>) {
          if (field == "device_id") {
            return a.device_id == b.device_id;
          }
          if (field == "session_id") {
            return a.session_id == b.session_id;
          }
          if (field == "workspace_id") {
            return a.workspace_id == b.workspace_id;
          }
          if (field == "url") {
            return a.url == b.url;
          }
          if (field == "title") {
            return a.title == b.title;
          }
          if (field == "opened_at") {
            return a.opened_at == b.opened_at;
          }
          if (field == "last_active") {
            return a.last_active == b.last_active;
          }
          if (field == "pinned") {
            return a.pinned == b.pinned;
          }
          if (field == "is_incognito") {
            return a.is_incognito == b.is_incognito;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, DeviceSessionRecord>) {
          if (field == "device_id") {
            return a.device_id == b.device_id;
          }
          if (field == "started_at") {
            return a.started_at == b.started_at;
          }
          if (field == "liveness") {
            return a.last_seen == b.last_seen && a.active == b.active;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, RemoteCommandRecord>) {
          if (field == "request") {
            return a.source_device_id == b.source_device_id &&
                   a.target_device_id == b.target_device_id &&
                   a.nonce_base64 == b.nonce_base64 &&
                   a.issued_at == b.issued_at && a.expires_at == b.expires_at &&
                   a.kind == b.kind && a.workspace_id == b.workspace_id &&
                   a.tab_id == b.tab_id && a.url == b.url &&
                   a.signature_base64 == b.signature_base64;
          }
          if (field == "status") {
            return a.status == b.status && a.result_code == b.result_code;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, AppearanceRecord>) {
          if (field == "color_mode") {
            return a.color_mode == b.color_mode;
          }
          if (field == "accent_argb") {
            return a.accent_argb == b.accent_argb;
          }
          if (field == "use_system_accent") {
            return a.use_system_accent == b.use_system_accent;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, PermittedSettingRecord>) {
          if (field == "setting_id") {
            return a.setting_id == b.setting_id;
          }
          if (field == "value_json") {
            return a.value_json == b.value_json;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, ExtensionInventoryRecord>) {
          if (field == "device_id") {
            return a.device_id == b.device_id;
          }
          if (field == "extension_id") {
            return a.extension_id == b.extension_id;
          }
          if (field == "name") {
            return a.name == b.name;
          }
          if (field == "extension_version") {
            return a.extension_version == b.extension_version;
          }
          if (field == "enabled") {
            return a.enabled == b.enabled;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, BookmarkRecord>) {
          if (field == "location") {
            return a.root_kind == b.root_kind && a.parent_id == b.parent_id &&
                   a.sort_key == b.sort_key;
          }
          if (field == "kind") {
            return a.kind == b.kind;
          }
          if (field == "title") {
            return a.title == b.title;
          }
          if (field == "url") {
            return a.url == b.url;
          }
          if (field == "created_at") {
            return a.created_at == b.created_at;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else {
          static_assert(std::is_same_v<A, DeveloperAssetRecord>);
          if (field == "kind") {
            return a.kind == b.kind;
          }
          if (field == "name") {
            return a.name == b.name;
          }
          if (field == "scope") {
            return a.scope == b.scope;
          }
          if (field == "source") {
            return a.source == b.source;
          }
          if (field == "enabled") {
            return a.enabled == b.enabled;
          }
          if (field == "opted_in") {
            return a.opted_in == b.opted_in;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        }
      },
      left, right);
}

void CopyField(const SyncRecord& source,
               std::string_view field,
               SyncRecord* destination) {
  std::visit(
      [field](const auto& from, auto& to) {
        using From = std::decay_t<decltype(from)>;
        using To = std::decay_t<decltype(to)>;
        if constexpr (!std::is_same_v<From, To>) {
          return;
        } else if constexpr (std::is_same_v<From, DeviceRecord>) {
          if (field == "type") {
            to.type = from.type;
          } else if (field == "display_name") {
            to.display_name = from.display_name;
          } else if (field == "created_at") {
            to.created_at = from.created_at;
          } else if (field == "last_seen") {
            to.last_seen = from.last_seen;
          } else if (field == "retired") {
            to.retired = from.retired;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, WorkspaceRecord>) {
          if (field == "name") {
            to.name = from.name;
          } else if (field == "icon") {
            to.icon = from.icon;
          } else if (field == "sort_key") {
            to.sort_key = from.sort_key;
          } else if (field == "accent_argb") {
            to.accent_argb = from.accent_argb;
          } else if (field == "created_at") {
            to.created_at = from.created_at;
          } else if (field == "modified_at") {
            to.modified_at = from.modified_at;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, TreeNodeRecord>) {
          if (field == "location") {
            to.workspace_id = from.workspace_id;
            to.parent_id = from.parent_id;
            to.sort_key = from.sort_key;
          } else if (field == "kind") {
            to.kind = from.kind;
          } else if (field == "title") {
            to.title = from.title;
          } else if (field == "icon") {
            to.icon = from.icon;
          } else if (field == "accent_argb") {
            to.accent_argb = from.accent_argb;
          } else if (field == "url") {
            to.url = from.url;
          } else if (field == "created_at") {
            to.created_at = from.created_at;
          } else if (field == "modified_at") {
            to.modified_at = from.modified_at;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, HistoryRecord>) {
          if (field == "device_id") {
            to.device_id = from.device_id;
          } else if (field == "url") {
            to.url = from.url;
          } else if (field == "title") {
            to.title = from.title;
          } else if (field == "last_visit") {
            to.last_visit = from.last_visit;
          } else if (field == "visit_count") {
            to.visit_count = from.visit_count;
          } else if (field == "transition") {
            to.transition = from.transition;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, RemoteTabRecord>) {
          if (field == "device_id") {
            to.device_id = from.device_id;
          } else if (field == "session_id") {
            to.session_id = from.session_id;
          } else if (field == "workspace_id") {
            to.workspace_id = from.workspace_id;
          } else if (field == "url") {
            to.url = from.url;
          } else if (field == "title") {
            to.title = from.title;
          } else if (field == "opened_at") {
            to.opened_at = from.opened_at;
          } else if (field == "last_active") {
            to.last_active = from.last_active;
          } else if (field == "pinned") {
            to.pinned = from.pinned;
          } else if (field == "is_incognito") {
            to.is_incognito = from.is_incognito;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, DeviceSessionRecord>) {
          if (field == "device_id") {
            to.device_id = from.device_id;
          } else if (field == "started_at") {
            to.started_at = from.started_at;
          } else if (field == "liveness") {
            to.last_seen = from.last_seen;
            to.active = from.active;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, RemoteCommandRecord>) {
          if (field == "request") {
            to.source_device_id = from.source_device_id;
            to.target_device_id = from.target_device_id;
            to.nonce_base64 = from.nonce_base64;
            to.issued_at = from.issued_at;
            to.expires_at = from.expires_at;
            to.kind = from.kind;
            to.workspace_id = from.workspace_id;
            to.tab_id = from.tab_id;
            to.url = from.url;
            to.signature_base64 = from.signature_base64;
          } else if (field == "status") {
            to.status = from.status;
            to.result_code = from.result_code;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, AppearanceRecord>) {
          if (field == "color_mode") {
            to.color_mode = from.color_mode;
          } else if (field == "accent_argb") {
            to.accent_argb = from.accent_argb;
          } else if (field == "use_system_accent") {
            to.use_system_accent = from.use_system_accent;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, PermittedSettingRecord>) {
          if (field == "setting_id") {
            to.setting_id = from.setting_id;
          } else if (field == "value_json") {
            to.value_json = from.value_json;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, ExtensionInventoryRecord>) {
          if (field == "device_id") {
            to.device_id = from.device_id;
          } else if (field == "extension_id") {
            to.extension_id = from.extension_id;
          } else if (field == "name") {
            to.name = from.name;
          } else if (field == "extension_version") {
            to.extension_version = from.extension_version;
          } else if (field == "enabled") {
            to.enabled = from.enabled;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, BookmarkRecord>) {
          if (field == "location") {
            to.root_kind = from.root_kind;
            to.parent_id = from.parent_id;
            to.sort_key = from.sort_key;
          } else if (field == "kind") {
            to.kind = from.kind;
          } else if (field == "title") {
            to.title = from.title;
          } else if (field == "url") {
            to.url = from.url;
          } else if (field == "created_at") {
            to.created_at = from.created_at;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else {
          static_assert(std::is_same_v<From, DeveloperAssetRecord>);
          if (field == "kind") {
            to.kind = from.kind;
          } else if (field == "name") {
            to.name = from.name;
          } else if (field == "scope") {
            to.scope = from.scope;
          } else if (field == "source") {
            to.source = from.source;
          } else if (field == "enabled") {
            to.enabled = from.enabled;
          } else if (field == "opted_in") {
            to.opted_in = from.opted_in;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        }
      },
      source, *destination);
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
  if (!record) {
    SetError("missing record", error);
    return false;
  }
  const EntityType type = GetEntityType(*record);
  FieldVersionMap& versions = Versions(record);
  for (const auto& [field, stamp] : versions) {
    if (!IsKnownField(type, field) || stamp.physical_time_us < 0 ||
        stamp.device_tiebreak.empty()) {
      SetError("invalid field version", error);
      return false;
    }
  }
  for (std::string_view field : FieldNames(type)) {
    versions.try_emplace(std::string(field), GetVersion(*record).stamp);
  }
  return true;
}

bool HasCompleteFieldVersions(const SyncRecord& record) {
  if (GetVersion(record).model_version < 2) {
    return true;
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
  if (!NormalizeFieldVersions(&normalized_existing, error) ||
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
  if (!NormalizeFieldVersions(&old_value, error) ||
      !NormalizeFieldVersions(&new_value, error)) {
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
