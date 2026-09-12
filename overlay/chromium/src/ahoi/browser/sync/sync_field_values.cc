// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_field_values.h"

#include <type_traits>

namespace ahoi::sync::field_internal {

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
          if (field == "archive_policy")
            return a.archive_policy == b.archive_policy;
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
          if (field == "home_target")
            return a.home_target == b.home_target;
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
            return a.url == b.url && a.target_kind == b.target_kind &&
                   a.local_scheme == b.local_scheme;
          }
          if (field == "is_temporary") {
            return a.is_temporary == b.is_temporary;
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
            return a.url == b.url && a.target_kind == b.target_kind &&
                   a.local_scheme == b.local_scheme;
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
          if (field == "tree_node_id") {
            return a.tree_node_id == b.tree_node_id;
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
        } else if constexpr (std::is_same_v<A, DeviceCapabilityRecord>) {
          if (field == "device_id") {
            return a.device_id == b.device_id;
          }
          if (field == "capabilities") {
            return a.readable_models == b.readable_models &&
                   a.writable_models == b.writable_models &&
                   a.features == b.features;
          }
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, SplitGroupRecord>) {
          if (field == "workspace_id")
            return a.workspace_id == b.workspace_id;
          if (field == "topology")
            return a.topology == b.topology;
          if (field == "ratios")
            return a.ratios == b.ratios;
          return field == "tombstone" && a.tombstone == b.tombstone;
        } else if constexpr (std::is_same_v<A, TabArchiveEntryRecord>) {
          if (field == "snapshot")
            return a.snapshot == b.snapshot;
          if (field == "state")
            return a.reason == b.reason && a.archived_at == b.archived_at &&
                   a.restored == b.restored;
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
          if (field == "archive_policy")
            to.archive_policy = from.archive_policy;
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
          if (field == "home_target")
            to.home_target = from.home_target;
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
            to.target_kind = from.target_kind;
            to.local_scheme = from.local_scheme;
          } else if (field == "is_temporary") {
            to.is_temporary = from.is_temporary;
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
            to.target_kind = from.target_kind;
            to.local_scheme = from.local_scheme;
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
          } else if (field == "tree_node_id") {
            to.tree_node_id = from.tree_node_id;
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
        } else if constexpr (std::is_same_v<From, DeviceCapabilityRecord>) {
          if (field == "device_id") {
            to.device_id = from.device_id;
          } else if (field == "capabilities") {
            to.readable_models = from.readable_models;
            to.writable_models = from.writable_models;
            to.features = from.features;
          } else if (field == "tombstone") {
            to.tombstone = from.tombstone;
          }
        } else if constexpr (std::is_same_v<From, SplitGroupRecord>) {
          if (field == "workspace_id")
            to.workspace_id = from.workspace_id;
          if (field == "topology")
            to.topology = from.topology;
          if (field == "ratios")
            to.ratios = from.ratios;
          if (field == "tombstone")
            to.tombstone = from.tombstone;
        } else if constexpr (std::is_same_v<From, TabArchiveEntryRecord>) {
          if (field == "snapshot")
            to.snapshot = from.snapshot;
          if (field == "state") {
            to.reason = from.reason;
            to.archived_at = from.archived_at;
            to.restored = from.restored;
          }
          if (field == "tombstone")
            to.tombstone = from.tombstone;
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

}  // namespace ahoi::sync::field_internal
