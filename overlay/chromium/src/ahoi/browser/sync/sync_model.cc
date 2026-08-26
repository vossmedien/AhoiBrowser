// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/sync_model.h"

#include <type_traits>

namespace ahoi::sync {

EntityType GetEntityType(const SyncRecord& record) {
  return std::visit(
      [](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DeviceRecord>) {
          return EntityType::kDevice;
        } else if constexpr (std::is_same_v<T, WorkspaceRecord>) {
          return EntityType::kWorkspace;
        } else if constexpr (std::is_same_v<T, TreeNodeRecord>) {
          return EntityType::kTreeNode;
        } else if constexpr (std::is_same_v<T, HistoryRecord>) {
          return EntityType::kHistoryEntry;
        } else if constexpr (std::is_same_v<T, RemoteTabRecord>) {
          return EntityType::kRemoteTab;
        } else if constexpr (std::is_same_v<T, DeviceSessionRecord>) {
          return EntityType::kDeviceSession;
        } else if constexpr (std::is_same_v<T, RemoteCommandRecord>) {
          return EntityType::kRemoteCommand;
        } else if constexpr (std::is_same_v<T, AppearanceRecord>) {
          return EntityType::kAppearance;
        } else if constexpr (std::is_same_v<T, PermittedSettingRecord>) {
          return EntityType::kPermittedSetting;
        } else if constexpr (std::is_same_v<T, ExtensionInventoryRecord>) {
          return EntityType::kExtensionInventory;
        } else {
          static_assert(std::is_same_v<T, DeveloperAssetRecord>);
          return EntityType::kDeveloperAsset;
        }
      },
      record);
}

const base::Uuid& GetEntityId(const SyncRecord& record) {
  return std::visit(
      [](const auto& value) -> const base::Uuid& { return value.id; }, record);
}

const SyncVersion& GetVersion(const SyncRecord& record) {
  return std::visit(
      [](const auto& value) -> const SyncVersion& { return value.version; },
      record);
}

bool IsTombstone(const SyncRecord& record) {
  return std::visit([](const auto& value) { return value.tombstone; }, record);
}

}  // namespace ahoi::sync
