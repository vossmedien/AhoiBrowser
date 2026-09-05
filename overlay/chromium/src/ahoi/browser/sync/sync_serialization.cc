// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/sync_serialization.h"

#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/bookmark_sync_serialization.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization_internal.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"

namespace ahoi::sync {
namespace {

using Dict = base::DictValue;

using serialization_internal::ParseDict;
using serialization_internal::ReadCommon;
using serialization_internal::ReadString;
using serialization_internal::ReadTime;
using serialization_internal::ReadUuid;
using serialization_internal::SetCommon;
using serialization_internal::SetTime;
using serialization_internal::WriteDict;

bool SerializeDevice(const DeviceRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("device_type", static_cast<int>(record.type));
  dict.Set("display_name", record.display_name);
  SetTime(dict, "created_at", record.created_at);
  SetTime(dict, "last_seen", record.last_seen);
  dict.Set("retired", record.retired);
  return WriteDict(dict, payload);
}

bool SerializeWorkspace(const WorkspaceRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("name", record.name);
  dict.Set("icon", record.icon);
  dict.Set("sort_key", record.sort_key);
  if (record.accent_argb) {
    dict.Set("accent_argb", static_cast<int>(record.accent_argb.value()));
  }
  SetTime(dict, "created_at", record.created_at);
  SetTime(dict, "modified_at", record.modified_at);
  return WriteDict(dict, payload);
}

bool SerializeTreeNode(const TreeNodeRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("workspace_id", record.workspace_id.AsLowercaseString());
  if (record.parent_id) {
    dict.Set("parent_id", record.parent_id->AsLowercaseString());
  }
  dict.Set("node_kind", static_cast<int>(record.kind));
  dict.Set("title", record.title);
  dict.Set("icon", record.icon);
  if (record.accent_argb) {
    dict.Set("accent_argb", static_cast<int>(record.accent_argb.value()));
  }
  dict.Set("url", record.url);
  dict.Set("sort_key", record.sort_key);
  SetTime(dict, "created_at", record.created_at);
  SetTime(dict, "modified_at", record.modified_at);
  return WriteDict(dict, payload);
}

bool SerializeHistory(const HistoryRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  if (record.device_id.is_valid()) {
    dict.Set("device_id", record.device_id.AsLowercaseString());
  }
  dict.Set("url", record.url);
  dict.Set("title", record.title);
  SetTime(dict, "last_visit", record.last_visit);
  dict.Set("visit_count", base::NumberToString(record.visit_count));
  dict.Set("transition", record.transition);
  return WriteDict(dict, payload);
}

bool SerializeRemoteTab(const RemoteTabRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("device_id", record.device_id.AsLowercaseString());
  dict.Set("session_id", record.session_id.AsLowercaseString());
  if (record.workspace_id) {
    dict.Set("workspace_id", record.workspace_id->AsLowercaseString());
  }
  dict.Set("url", record.url);
  dict.Set("title", record.title);
  SetTime(dict, "opened_at", record.opened_at);
  SetTime(dict, "last_active", record.last_active);
  dict.Set("pinned", record.pinned);
  dict.Set("is_incognito", record.is_incognito);
  return WriteDict(dict, payload);
}

bool SerializeSession(const DeviceSessionRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("device_id", record.device_id.AsLowercaseString());
  SetTime(dict, "started_at", record.started_at);
  SetTime(dict, "last_seen", record.last_seen);
  dict.Set("active", record.active);
  return WriteDict(dict, payload);
}

bool SerializeRemoteCommand(const RemoteCommandRecord& record,
                            std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("source_device_id", record.source_device_id.AsLowercaseString());
  dict.Set("target_device_id", record.target_device_id.AsLowercaseString());
  dict.Set("nonce", record.nonce_base64);
  SetTime(dict, "issued_at", record.issued_at);
  SetTime(dict, "expires_at", record.expires_at);
  dict.Set("command_kind", static_cast<int>(record.kind));
  if (record.workspace_id) {
    dict.Set("workspace_id", record.workspace_id->AsLowercaseString());
  }
  if (record.tab_id) {
    dict.Set("tab_id", record.tab_id->AsLowercaseString());
  }
  dict.Set("url", record.url);
  dict.Set("signature", record.signature_base64);
  dict.Set("status", static_cast<int>(record.status));
  dict.Set("result", record.result_code);
  return WriteDict(dict, payload);
}

bool SerializeAppearance(const AppearanceRecord& record, std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("color_mode", record.color_mode);
  if (record.accent_argb) {
    dict.Set("accent_argb", static_cast<int>(record.accent_argb.value()));
  }
  dict.Set("use_system_accent", record.use_system_accent);
  return WriteDict(dict, payload);
}

bool SerializePermittedSetting(const PermittedSettingRecord& record,
                               std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("setting_id", record.setting_id);
  dict.Set("value_json", record.value_json);
  return WriteDict(dict, payload);
}

bool SerializeExtensionInventory(const ExtensionInventoryRecord& record,
                                 std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("device_id", record.device_id.AsLowercaseString());
  dict.Set("extension_id", record.extension_id);
  dict.Set("name", record.name);
  dict.Set("extension_version", record.extension_version);
  dict.Set("enabled", record.enabled);
  return WriteDict(dict, payload);
}

bool SerializeDeveloperAsset(const DeveloperAssetRecord& record,
                             std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("asset_kind", static_cast<int>(record.kind));
  dict.Set("name", record.name);
  dict.Set("scope", record.scope);
  dict.Set("source", record.source);
  dict.Set("enabled", record.enabled);
  dict.Set("opted_in", record.opted_in);
  return WriteDict(dict, payload);
}

bool DeserializeDevice(const Dict& dict, DeviceRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "display_name", &record->display_name) ||
      !ReadTime(dict, "created_at", &record->created_at) ||
      !ReadTime(dict, "last_seen", &record->last_seen)) {
    return false;
  }
  const std::optional<int> type = dict.FindInt("device_type");
  const std::optional<bool> retired = dict.FindBool("retired");
  if (!type || *type < static_cast<int>(DeviceType::kMacDesktop) ||
      *type > static_cast<int>(DeviceType::kOther) || !retired) {
    return false;
  }
  record->type = static_cast<DeviceType>(*type);
  record->retired = *retired;
  return true;
}

bool DeserializeWorkspace(const Dict& dict, WorkspaceRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "name", &record->name) ||
      !ReadString(dict, "icon", &record->icon) ||
      !ReadString(dict, "sort_key", &record->sort_key) ||
      !ReadTime(dict, "created_at", &record->created_at) ||
      !ReadTime(dict, "modified_at", &record->modified_at)) {
    return false;
  }
  const base::Value* accent = dict.Find("accent_argb");
  if (accent) {
    if (!accent->is_int()) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(accent->GetInt());
  }
  return true;
}

bool DeserializeTreeNode(const Dict& dict, TreeNodeRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "workspace_id", &record->workspace_id, false) ||
      !ReadString(dict, "title", &record->title) ||
      !ReadString(dict, "url", &record->url) ||
      !ReadString(dict, "sort_key", &record->sort_key) ||
      !ReadTime(dict, "created_at", &record->created_at) ||
      !ReadTime(dict, "modified_at", &record->modified_at)) {
    return false;
  }
  base::Uuid parent;
  if (!ReadUuid(dict, "parent_id", &parent, true)) {
    return false;
  }
  if (parent.is_valid()) {
    record->parent_id = parent;
  }
  const std::optional<int> kind = dict.FindInt("node_kind");
  if (!kind || *kind < static_cast<int>(TreeNodeKind::kFolder) ||
      *kind > static_cast<int>(TreeNodeKind::kPage)) {
    return false;
  }
  record->kind = static_cast<TreeNodeKind>(*kind);
  if (const std::string* icon = dict.FindString("icon")) {
    record->icon = *icon;
  }
  if (const base::Value* accent = dict.Find("accent_argb")) {
    if (!accent->is_int()) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(accent->GetInt());
  }
  return true;
}

bool DeserializeHistory(const Dict& dict, HistoryRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "url", &record->url) ||
      !ReadString(dict, "title", &record->title) ||
      !ReadTime(dict, "last_visit", &record->last_visit)) {
    return false;
  }
  const std::string* count = dict.FindString("visit_count");
  if (!count || !base::StringToInt64(*count, &record->visit_count) ||
      record->visit_count < 0) {
    return false;
  }
  base::Uuid device_id;
  if (!ReadUuid(dict, "device_id", &device_id, true)) {
    return false;
  }
  if (device_id.is_valid()) {
    record->device_id = device_id;
  }
  if (const std::string* transition = dict.FindString("transition")) {
    record->transition = *transition;
  }
  return true;
}

bool DeserializeRemoteTab(const Dict& dict, RemoteTabRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "device_id", &record->device_id, false) ||
      !ReadUuid(dict, "session_id", &record->session_id, false) ||
      !ReadString(dict, "url", &record->url) ||
      !ReadString(dict, "title", &record->title) ||
      !ReadTime(dict, "opened_at", &record->opened_at) ||
      !ReadTime(dict, "last_active", &record->last_active)) {
    return false;
  }
  base::Uuid workspace;
  if (!ReadUuid(dict, "workspace_id", &workspace, true)) {
    return false;
  }
  if (workspace.is_valid()) {
    record->workspace_id = workspace;
  }
  const std::optional<bool> pinned = dict.FindBool("pinned");
  const std::optional<bool> incognito = dict.FindBool("is_incognito");
  if (!pinned || !incognito) {
    return false;
  }
  record->pinned = *pinned;
  record->is_incognito = *incognito;
  return true;
}

bool DeserializeSession(const Dict& dict, DeviceSessionRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "device_id", &record->device_id, false) ||
      !ReadTime(dict, "started_at", &record->started_at) ||
      !ReadTime(dict, "last_seen", &record->last_seen)) {
    return false;
  }
  const std::optional<bool> active = dict.FindBool("active");
  if (!active) {
    return false;
  }
  record->active = *active;
  return true;
}

bool DeserializeRemoteCommand(const Dict& dict, RemoteCommandRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "source_device_id", &record->source_device_id, false) ||
      !ReadUuid(dict, "target_device_id", &record->target_device_id, false) ||
      !ReadString(dict, "nonce", &record->nonce_base64) ||
      !ReadTime(dict, "issued_at", &record->issued_at) ||
      !ReadTime(dict, "expires_at", &record->expires_at) ||
      !ReadString(dict, "url", &record->url) ||
      !ReadString(dict, "signature", &record->signature_base64) ||
      !ReadString(dict, "result", &record->result_code)) {
    return false;
  }
  base::Uuid workspace;
  base::Uuid tab;
  if (!ReadUuid(dict, "workspace_id", &workspace, true) ||
      !ReadUuid(dict, "tab_id", &tab, true)) {
    return false;
  }
  if (workspace.is_valid()) {
    record->workspace_id = workspace;
  }
  if (tab.is_valid()) {
    record->tab_id = tab;
  }
  const std::optional<int> kind = dict.FindInt("command_kind");
  const std::optional<int> status = dict.FindInt("status");
  if (!kind || *kind < static_cast<int>(RemoteCommandKind::kOpen) ||
      *kind > static_cast<int>(RemoteCommandKind::kClose) || !status ||
      *status < static_cast<int>(RemoteCommandStatus::kQueued) ||
      *status > static_cast<int>(RemoteCommandStatus::kFailed)) {
    return false;
  }
  record->kind = static_cast<RemoteCommandKind>(*kind);
  record->status = static_cast<RemoteCommandStatus>(*status);
  return true;
}

bool DeserializeAppearance(const Dict& dict, AppearanceRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "color_mode", &record->color_mode)) {
    return false;
  }
  if (const base::Value* accent = dict.Find("accent_argb")) {
    if (!accent->is_int()) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(accent->GetInt());
  }
  const std::optional<bool> system = dict.FindBool("use_system_accent");
  if (!system) {
    return false;
  }
  record->use_system_accent = *system;
  return true;
}

bool DeserializePermittedSetting(const Dict& dict,
                                 PermittedSettingRecord* record) {
  return ReadCommon(dict, &record->model_version, &record->id,
                    &record->tombstone, &record->version,
                    &record->field_versions) &&
         ReadString(dict, "setting_id", &record->setting_id) &&
         ReadString(dict, "value_json", &record->value_json);
}

bool DeserializeExtensionInventory(const Dict& dict,
                                   ExtensionInventoryRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "device_id", &record->device_id, false) ||
      !ReadString(dict, "extension_id", &record->extension_id) ||
      !ReadString(dict, "name", &record->name) ||
      !ReadString(dict, "extension_version", &record->extension_version)) {
    return false;
  }
  const std::optional<bool> enabled = dict.FindBool("enabled");
  if (!enabled) {
    return false;
  }
  record->enabled = *enabled;
  return true;
}

bool DeserializeDeveloperAsset(const Dict& dict, DeveloperAssetRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "name", &record->name) ||
      !ReadString(dict, "scope", &record->scope) ||
      !ReadString(dict, "source", &record->source)) {
    return false;
  }
  const std::optional<int> kind = dict.FindInt("asset_kind");
  const std::optional<bool> enabled = dict.FindBool("enabled");
  const std::optional<bool> opted_in = dict.FindBool("opted_in");
  if (!kind || *kind < static_cast<int>(DeveloperAssetKind::kCss) ||
      *kind > static_cast<int>(DeveloperAssetKind::kHeaderProfile) ||
      !enabled || !opted_in) {
    return false;
  }
  record->kind = static_cast<DeveloperAssetKind>(*kind);
  record->enabled = *enabled;
  record->opted_in = *opted_in;
  return true;
}

}  // namespace

bool SerializeRecord(const SyncRecord& record, std::string* payload) {
  if (!payload) {
    return false;
  }
  SyncRecord normalized = record;
  if (!NormalizeFieldVersions(&normalized, nullptr)) {
    return false;
  }
  return std::visit(
      [payload](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DeviceRecord>) {
          return SerializeDevice(value, payload);
        } else if constexpr (std::is_same_v<T, WorkspaceRecord>) {
          return SerializeWorkspace(value, payload);
        } else if constexpr (std::is_same_v<T, TreeNodeRecord>) {
          return SerializeTreeNode(value, payload);
        } else if constexpr (std::is_same_v<T, HistoryRecord>) {
          return SerializeHistory(value, payload);
        } else if constexpr (std::is_same_v<T, RemoteTabRecord>) {
          return SerializeRemoteTab(value, payload);
        } else if constexpr (std::is_same_v<T, DeviceSessionRecord>) {
          return SerializeSession(value, payload);
        } else if constexpr (std::is_same_v<T, RemoteCommandRecord>) {
          return SerializeRemoteCommand(value, payload);
        } else if constexpr (std::is_same_v<T, AppearanceRecord>) {
          return SerializeAppearance(value, payload);
        } else if constexpr (std::is_same_v<T, PermittedSettingRecord>) {
          return SerializePermittedSetting(value, payload);
        } else if constexpr (std::is_same_v<T, ExtensionInventoryRecord>) {
          return SerializeExtensionInventory(value, payload);
        } else if constexpr (std::is_same_v<T, DeveloperAssetRecord>) {
          return SerializeDeveloperAsset(value, payload);
        } else {
          static_assert(std::is_same_v<T, BookmarkRecord>);
          return serialization_internal::SerializeBookmark(value, payload);
        }
      },
      normalized);
}

bool DeserializeRecord(EntityType expected_type,
                       const std::string& payload,
                       SyncRecord* record) {
  std::optional<Dict> dict = ParseDict(payload);
  if (!dict || !record) {
    return false;
  }
  SyncRecord decoded;
  switch (expected_type) {
    case EntityType::kDevice: {
      DeviceRecord value;
      if (!DeserializeDevice(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kWorkspace: {
      WorkspaceRecord value;
      if (!DeserializeWorkspace(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kTreeNode: {
      TreeNodeRecord value;
      if (!DeserializeTreeNode(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kHistoryEntry: {
      HistoryRecord value;
      if (!DeserializeHistory(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kRemoteTab: {
      RemoteTabRecord value;
      if (!DeserializeRemoteTab(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kDeviceSession: {
      DeviceSessionRecord value;
      if (!DeserializeSession(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kRemoteCommand: {
      RemoteCommandRecord value;
      if (!DeserializeRemoteCommand(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kAppearance: {
      AppearanceRecord value;
      if (!DeserializeAppearance(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kPermittedSetting: {
      PermittedSettingRecord value;
      if (!DeserializePermittedSetting(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kExtensionInventory: {
      ExtensionInventoryRecord value;
      if (!DeserializeExtensionInventory(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kDeveloperAsset: {
      DeveloperAssetRecord value;
      if (!DeserializeDeveloperAsset(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
    case EntityType::kBookmark: {
      BookmarkRecord value;
      if (!serialization_internal::DeserializeBookmark(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
  }
  if (!HasCompleteFieldVersions(decoded)) {
    return false;
  }
  *record = std::move(decoded);
  return true;
}

bool ValidateChangeEnvelope(const SyncChange& change, SyncRecord* decoded) {
  if (change.mutation_id.empty() || !change.entity_id.is_valid() ||
      (change.kind != ChangeKind::kUpsert &&
       change.kind != ChangeKind::kDelete) ||
      change.version.stamp.device_tiebreak.empty() || change.payload.empty() ||
      !DeserializeRecord(change.entity_type, change.payload, decoded)) {
    return false;
  }
  if (GetEntityType(*decoded) != change.entity_type ||
      GetEntityId(*decoded) != change.entity_id ||
      GetVersion(*decoded) != change.version ||
      (change.kind == ChangeKind::kDelete) != IsTombstone(*decoded)) {
    return false;
  }
  return true;
}

}  // namespace ahoi::sync
