// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/sync_serialization.h"

#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/bookmark_sync_serialization.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization_internal.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"

namespace ahoi::sync {
namespace {

using Dict = base::DictValue;

using serialization_internal::ParseDict;
using serialization_internal::ReadCommon;
using serialization_internal::ReadInt32;
using serialization_internal::ReadString;
using serialization_internal::ReadTime;
using serialization_internal::ReadUuid;
using serialization_internal::SetCommon;
using serialization_internal::SetTime;
using serialization_internal::WriteDict;

void SetTarget(Dict& dict,
               const std::optional<SharedTabTargetKind>& kind,
               const std::optional<std::string>& local_scheme) {
  if (kind) {
    dict.Set("target_kind", static_cast<int>(*kind));
  }
  if (local_scheme) {
    dict.Set("local_scheme", *local_scheme);
  }
}

bool ReadTarget(const Dict& dict,
                bool folder,
                std::optional<SharedTabTargetKind>* kind,
                std::optional<std::string>* local_scheme) {
  if (folder) {
    return !dict.contains("target_kind") && !dict.contains("local_scheme");
  }
  const std::optional<int> raw = ReadInt32(dict, "target_kind");
  if (!raw || *raw < 0 || *raw > 2) {
    return false;
  }
  *kind = static_cast<SharedTabTargetKind>(*raw);
  if (dict.contains("local_scheme")) {
    const auto* value = dict.FindString("local_scheme");
    if (!value) {
      return false;
    }
    *local_scheme = *value;
  }
  return true;
}

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
  dict.Set("archive_policy", static_cast<int>(record.archive_policy));
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
  dict.Set("is_temporary", record.is_temporary);
  SetTarget(dict, record.target_kind, record.local_scheme);
  serialization_internal::SetHomeTarget(dict, record.home_target);
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
  if (!record.tree_node_id) {
    return false;
  }
  dict.Set("tree_node_id", record.tree_node_id->AsLowercaseString());
  SetTarget(dict, record.target_kind, record.local_scheme);
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

bool SerializeCapability(const DeviceCapabilityRecord& record,
                         std::string* payload) {
  Dict dict;
  SetCommon(dict, record.model_version, record.id, record.tombstone,
            record.version, record.field_versions);
  dict.Set("device_id", record.device_id.AsLowercaseString());
  base::ListValue readers;
  base::ListValue writers;
  base::ListValue features;
  for (int value : record.readable_models) {
    readers.Append(value);
  }
  for (int value : record.writable_models) {
    writers.Append(value);
  }
  for (const auto& value : record.features) {
    features.Append(value);
  }
  dict.Set("readable_models", std::move(readers));
  dict.Set("writable_models", std::move(writers));
  dict.Set("features", std::move(features));
  return WriteDict(dict, payload);
}

bool DeserializeCapability(const Dict& dict, DeviceCapabilityRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadUuid(dict, "device_id", &record->device_id, false)) {
    return false;
  }
  const auto* readers = dict.FindList("readable_models");
  const auto* writers = dict.FindList("writable_models");
  const auto* features = dict.FindList("features");
  if (!readers || !writers || !features || readers->size() != 1 ||
      writers->size() != 1) {
    return false;
  }
  const auto reader = ReadInt32(readers->front());
  const auto writer = ReadInt32(writers->front());
  if (!reader || !writer) {
    return false;
  }
  record->readable_models = {*reader};
  record->writable_models = {*writer};
  for (const auto& feature : *features) {
    if (!feature.is_string()) {
      return false;
    }
    record->features.push_back(feature.GetString());
  }
  return true;
}

bool DeserializeDevice(const Dict& dict, DeviceRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "display_name", &record->display_name) ||
      !ReadTime(dict, "created_at", &record->created_at) ||
      !ReadTime(dict, "last_seen", &record->last_seen)) {
    return false;
  }
  const std::optional<int> type = ReadInt32(dict, "device_type");
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
  const auto policy = ReadInt32(dict, "archive_policy");
  if (!policy || *policy < 0 || *policy > 4)
    return false;
  record->archive_policy = static_cast<SharedArchivePolicy>(*policy);
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
    const auto parsed = ReadInt32(*accent);
    if (!parsed) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(*parsed);
  }
  return true;
}

bool DeserializeTreeNode(const Dict& dict, TreeNodeRecord* record) {
  if (!serialization_internal::ReadHomeTarget(dict, &record->home_target))
    return false;
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
  const std::optional<int> kind = ReadInt32(dict, "node_kind");
  if (!kind || *kind < static_cast<int>(TreeNodeKind::kFolder) ||
      *kind > static_cast<int>(TreeNodeKind::kPage)) {
    return false;
  }
  record->kind = static_cast<TreeNodeKind>(*kind);
  const auto temporary = dict.FindBool("is_temporary");
  if (!temporary || !ReadTarget(dict, record->kind == TreeNodeKind::kFolder,
                                &record->target_kind, &record->local_scheme)) {
    return false;
  }
  record->is_temporary = *temporary;
  if (const std::string* icon = dict.FindString("icon")) {
    record->icon = *icon;
  }
  if (const base::Value* accent = dict.Find("accent_argb")) {
    const auto parsed = ReadInt32(*accent);
    if (!parsed) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(*parsed);
  }
  return true;
}

bool DeserializeHistory(const Dict& dict, HistoryRecord* record) {
  if (!ReadCommon(dict, &record->model_version, &record->id, &record->tombstone,
                  &record->version, &record->field_versions) ||
      !ReadString(dict, "url", &record->url) ||
      !ReadString(dict, "title", &record->title) ||
      !ReadString(dict, "transition", &record->transition) ||
      !ReadTime(dict, "last_visit", &record->last_visit)) {
    return false;
  }
  const std::string* count = dict.FindString("visit_count");
  if (!count || !base::StringToInt64(*count, &record->visit_count) ||
      record->visit_count < 0) {
    return false;
  }
  base::Uuid device_id;
  if (!ReadUuid(dict, "device_id", &device_id, false)) {
    return false;
  }
  if (device_id.is_valid()) {
    record->device_id = device_id;
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
  base::Uuid tree_node_id;
  if (!ReadUuid(dict, "tree_node_id", &tree_node_id, false) ||
      !ReadTarget(dict, false, &record->target_kind, &record->local_scheme)) {
    return false;
  }
  record->tree_node_id = tree_node_id;
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
  const std::optional<int> kind = ReadInt32(dict, "command_kind");
  const std::optional<int> status = ReadInt32(dict, "status");
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
    const auto parsed = ReadInt32(*accent);
    if (!parsed) {
      return false;
    }
    record->accent_argb = static_cast<uint32_t>(*parsed);
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
  const std::optional<int> kind = ReadInt32(dict, "asset_kind");
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
  if (!NormalizeFieldVersions(&normalized, nullptr) ||
      !ValidateRecord(normalized)) {
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
        } else if constexpr (std::is_same_v<T, DeviceCapabilityRecord>) {
          return SerializeCapability(value, payload);
        } else if constexpr (std::is_same_v<T, SplitGroupRecord>) {
          return serialization_internal::SerializeSplitGroup(value, payload);
        } else if constexpr (std::is_same_v<T, TabArchiveEntryRecord>) {
          return serialization_internal::SerializeArchiveEntry(value, payload);
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
  if (expected_type == EntityType::kTabArchiveEntry &&
      payload.size() > 512 * 1024)
    return false;
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
    case EntityType::kSplitGroup: {
      SplitGroupRecord value;
      if (!serialization_internal::DeserializeSplitGroup(*dict, &value))
        return false;
      decoded = std::move(value);
      break;
    }
    case EntityType::kTabArchiveEntry: {
      TabArchiveEntryRecord value;
      if (!serialization_internal::DeserializeArchiveEntry(*dict, &value))
        return false;
      decoded = std::move(value);
      break;
    }
    case EntityType::kDeviceCapability: {
      DeviceCapabilityRecord value;
      if (!DeserializeCapability(*dict, &value)) {
        return false;
      }
      decoded = std::move(value);
      break;
    }
  }
  if (!HasCompleteFieldVersions(decoded) || !ValidateRecord(decoded)) {
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
