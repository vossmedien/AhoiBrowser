// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_MODEL_H_
#define AHOI_BROWSER_SYNC_SYNC_MODEL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/time/time.h"
#include "base/uuid.h"

namespace ahoi::sync {

// Bump this when a persisted record's meaning changes. The database schema
// version and this model version are intentionally separate: schema migrations
// describe storage, while this version is part of a record exchanged with an
// iOS client or another desktop build.
inline constexpr int kCurrentModelVersion = 2;
inline constexpr int kCurrentSchemaVersion = 4;

enum class DeviceType {
  kMacDesktop = 0,
  kIPhone = 1,
  kIPad = 2,
  kOther = 3,
};

enum class EntityType {
  kDevice = 0,
  kWorkspace = 1,
  kTreeNode = 2,
  kHistoryEntry = 3,
  kRemoteTab = 4,
  kDeviceSession = 5,
  kRemoteCommand = 6,
  kAppearance = 7,
  kPermittedSetting = 8,
  kExtensionInventory = 9,
  kDeveloperAsset = 10,
};

enum class ChangeKind {
  kUpsert = 0,
  kDelete = 1,
};

// Hybrid Logical Clock values are ordered by physical time, then logical
// counter, then the stable device id. The final component makes concurrent
// writes deterministic without depending on arrival order or provider order.
struct HlcStamp {
  int64_t physical_time_us = 0;
  uint32_t logical = 0;
  std::string device_tiebreak;

  friend bool operator==(const HlcStamp&, const HlcStamp&) = default;
  friend bool operator<(const HlcStamp& left, const HlcStamp& right) {
    if (left.physical_time_us != right.physical_time_us) {
      return left.physical_time_us < right.physical_time_us;
    }
    if (left.logical != right.logical) {
      return left.logical < right.logical;
    }
    return left.device_tiebreak < right.device_tiebreak;
  }
  friend bool operator>(const HlcStamp& left, const HlcStamp& right) {
    return right < left;
  }
  friend bool operator<=(const HlcStamp& left, const HlcStamp& right) {
    return !(right < left);
  }
  friend bool operator>=(const HlcStamp& left, const HlcStamp& right) {
    return !(left < right);
  }
};

// Wire-v2 assigns a clock to each independently mergeable field (or atomic
// field group such as a tree node's location). Unknown keys are rejected by
// the model validator. Wire-v1 records have no map; decoders synthesize the
// record clock for every known field so upgrades converge deterministically.
using FieldVersionMap = base::flat_map<std::string, HlcStamp>;

struct SyncVersion {
  int model_version = kCurrentModelVersion;
  HlcStamp stamp;

  friend bool operator==(const SyncVersion&, const SyncVersion&) = default;
  friend bool operator<(const SyncVersion& left, const SyncVersion& right) {
    return left.stamp < right.stamp;
  }
  friend bool operator>(const SyncVersion& left, const SyncVersion& right) {
    return right < left;
  }
  friend bool operator<=(const SyncVersion& left, const SyncVersion& right) {
    return !(right < left);
  }
  friend bool operator>=(const SyncVersion& left, const SyncVersion& right) {
    return !(left < right);
  }
};

struct DeviceRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  DeviceType type = DeviceType::kOther;
  std::string display_name;
  base::Time created_at;
  base::Time last_seen;
  bool retired = false;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const DeviceRecord&, const DeviceRecord&) = default;
};

struct WorkspaceRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  std::string name;
  std::string icon;
  std::string sort_key;
  std::optional<uint32_t> accent_argb;
  base::Time created_at;
  base::Time modified_at;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const WorkspaceRecord&,
                         const WorkspaceRecord&) = default;
};

enum class TreeNodeKind {
  kFolder = 0,
  kPage = 1,
};

struct TreeNodeRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid workspace_id;
  std::optional<base::Uuid> parent_id;
  TreeNodeKind kind = TreeNodeKind::kFolder;
  std::string title;
  std::string icon;
  std::optional<uint32_t> accent_argb;
  std::string url;
  std::string sort_key;
  base::Time created_at;
  base::Time modified_at;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const TreeNodeRecord&,
                         const TreeNodeRecord&) = default;
};

struct HistoryRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid device_id;
  std::string url;
  std::string title;
  base::Time last_visit;
  int64_t visit_count = 0;
  std::string transition;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const HistoryRecord&, const HistoryRecord&) = default;
};

enum class RemoteCommandKind {
  kOpen = 0,
  kFocus = 1,
  kClose = 2,
};

enum class RemoteCommandStatus {
  kQueued = 0,
  kDelivered = 1,
  kExecuted = 2,
  kFailed = 3,
};

// A target-bound, single-action control request. `signature_base64` signs only
// the immutable command payload (identity, devices, nonce, issue time and
// action); delivery status and the deliberately coarse result code are updated
// by the target. Private fields are carried only inside the encrypted provider
// value. Public keys and private signing keys are never sync records.
struct RemoteCommandRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid source_device_id;
  base::Uuid target_device_id;
  std::string nonce_base64;
  base::Time issued_at;
  base::Time expires_at;
  RemoteCommandKind kind = RemoteCommandKind::kOpen;
  std::optional<base::Uuid> workspace_id;
  std::optional<base::Uuid> tab_id;
  std::string url;
  std::string signature_base64;
  RemoteCommandStatus status = RemoteCommandStatus::kQueued;
  std::string result_code;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const RemoteCommandRecord&,
                         const RemoteCommandRecord&) = default;
};

// Remote tabs intentionally contain only normal browsing-session metadata.
// Cookies, credentials, web storage, cache and incognito state are never sync
// records. `is_incognito` is retained as a defensive wire-level field so a
// malformed provider payload can be rejected rather than accidentally shown.
struct RemoteTabRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid device_id;
  base::Uuid session_id;
  std::optional<base::Uuid> workspace_id;
  std::string url;
  std::string title;
  base::Time opened_at;
  base::Time last_active;
  bool pinned = false;
  bool is_incognito = false;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const RemoteTabRecord&,
                         const RemoteTabRecord&) = default;
};

struct DeviceSessionRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid device_id;
  base::Time started_at;
  base::Time last_seen;
  bool active = true;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const DeviceSessionRecord&,
                         const DeviceSessionRecord&) = default;
};

// Product appearance is a small, profile-wide register. Workspace-specific
// accents remain on WorkspaceRecord; this record carries only the global mode
// and accent preference that is safe to apply on another Ahoi device.
struct AppearanceRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  std::string color_mode;
  std::optional<uint32_t> accent_argb;
  bool use_system_accent = true;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const AppearanceRecord&,
                         const AppearanceRecord&) = default;
};

// Only IDs explicitly permitted by product/profile policy may be authored.
// The canonical JSON value remains encrypted by the provider and is never a
// generic Chromium PrefService replication channel.
struct PermittedSettingRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  std::string setting_id;
  std::string value_json;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const PermittedSettingRecord&,
                         const PermittedSettingRecord&) = default;
};

// Inventory is advisory metadata only. Receiving this record never installs
// or enables an extension and never transports extension storage.
struct ExtensionInventoryRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  base::Uuid device_id;
  std::string extension_id;
  std::string name;
  std::string extension_version;
  bool enabled = false;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const ExtensionInventoryRecord&,
                         const ExtensionInventoryRecord&) = default;
};

enum class DeveloperAssetKind {
  kCss = 0,
  kLess = 1,
  kSass = 2,
  kJavaScript = 3,
  kHeaderProfile = 4,
};

// Developer assets are present in the store/outbox only after per-asset
// opt-in. `source` is the shareable public body; Keychain values and secret
// header material have no field in this model and stay local.
struct DeveloperAssetRecord {
  int model_version = kCurrentModelVersion;
  base::Uuid id;
  DeveloperAssetKind kind = DeveloperAssetKind::kCss;
  std::string name;
  std::string scope;
  std::string source;
  bool enabled = false;
  bool opted_in = false;
  bool tombstone = false;
  SyncVersion version;
  FieldVersionMap field_versions;

  friend bool operator==(const DeveloperAssetRecord&,
                         const DeveloperAssetRecord&) = default;
};

// Deletions are retained separately from the materialized record payload so a
// provider can carry a delete after the last visible copy has been compacted.
// The SQLite store mirrors this value in `sync_tombstones` and keeps the full
// tombstoned record until the retention window and outbox acknowledgement
// permit compaction; a durable deletion watermark then rejects resurrection.
struct Tombstone {
  int model_version = kCurrentModelVersion;
  EntityType entity_type = EntityType::kDevice;
  base::Uuid entity_id;
  SyncVersion version;
  base::Time deleted_at;

  friend bool operator==(const Tombstone&, const Tombstone&) = default;
};

using SyncRecord = std::variant<DeviceRecord,
                                WorkspaceRecord,
                                TreeNodeRecord,
                                HistoryRecord,
                                RemoteTabRecord,
                                DeviceSessionRecord,
                                RemoteCommandRecord,
                                AppearanceRecord,
                                PermittedSettingRecord,
                                ExtensionInventoryRecord,
                                DeveloperAssetRecord>;

struct SyncChange {
  std::string mutation_id;
  EntityType entity_type = EntityType::kDevice;
  base::Uuid entity_id;
  ChangeKind kind = ChangeKind::kUpsert;
  SyncVersion version;
  // Canonical JSON for the record. Keeping the provider envelope opaque makes
  // CloudKit, a future relay, and an iOS client interchangeable at this seam.
  std::string payload;
};

struct ProviderBatch {
  std::vector<SyncChange> changes;
  std::string next_change_token;
  bool has_more = false;
};

struct RetryState {
  int attempt = 0;
  base::Time last_attempt;
  base::Time next_attempt;
  std::string last_error;

  friend bool operator==(const RetryState&, const RetryState&) = default;
};

struct SyncTransportStatus {
  bool enabled = false;
  bool provider_available = false;
  bool account_transition_pending = false;
  bool zone_recovery_pending = false;
  int pending_outbox = 0;
  RetryState retry;

  friend bool operator==(const SyncTransportStatus&,
                         const SyncTransportStatus&) = default;
};

struct DeviceTabsSnapshot {
  // Metadata is published in the same immutable snapshot as the tabs. UI
  // consumers therefore never have to issue a second, racy database lookup
  // to turn a device/workspace id into the label and icon shown beside a tab.
  std::vector<DeviceRecord> devices;
  std::vector<DeviceSessionRecord> sessions;
  std::vector<WorkspaceRecord> workspaces;
  std::vector<RemoteTabRecord> local_tabs;
  std::vector<RemoteTabRecord> remote_tabs;

  friend bool operator==(const DeviceTabsSnapshot&,
                         const DeviceTabsSnapshot&) = default;
};

// Immutable cross-sequence materialized state. UI consumers apply tree and
// history rows through their existing authorities; this is not a second
// mutable domain store.
struct SyncStateSnapshot {
  SyncTransportStatus transport;
  DeviceTabsSnapshot device_tabs;
  std::vector<WorkspaceRecord> workspaces;
  std::vector<TreeNodeRecord> tree_nodes;
  std::vector<HistoryRecord> history;
  std::vector<AppearanceRecord> appearance;
  std::vector<PermittedSettingRecord> permitted_settings;
  std::vector<ExtensionInventoryRecord> extension_inventory;
  std::vector<DeveloperAssetRecord> developer_assets;

  friend bool operator==(const SyncStateSnapshot&,
                         const SyncStateSnapshot&) = default;
};

EntityType GetEntityType(const SyncRecord& record);
const base::Uuid& GetEntityId(const SyncRecord& record);
const SyncVersion& GetVersion(const SyncRecord& record);
bool IsTombstone(const SyncRecord& record);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_MODEL_H_
