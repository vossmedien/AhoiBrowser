// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_NAMESPACE_H_
#define AHOI_BROWSER_SYNC_SYNC_NAMESPACE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/uuid.h"

namespace ahoi::sync {

// Default zone of the main Profile when Info.plist does not override it.
inline constexpr char kMainSyncZoneName[] = "AhoiBrowserSyncV3";
// A fully separated Workspace's zone is `<main zone>-ws-<uuid lowercase>`;
// the Companion discovers these zones by this prefix.
inline constexpr char kWorkspaceZoneInfix[] = "-ws-";
// Its end-to-end key lives under `<main key account>.ws-<uuid lowercase>`.
inline constexpr char kWorkspaceKeyAccountInfix[] = ".ws-";
// CloudKit zone names: ASCII letters, digits, '-' and '_', at most 255
// characters, not starting with '_' (reserved, e.g. `_defaultZone`).
inline constexpr size_t kMaxCloudKitZoneNameLength = 255;

// ADR 0011 step 4: the CloudKit namespace one Profile syncs into. The main
// Profile (and any ordinary, non-separated Profile) uses the main namespace;
// a fully separated Workspace's Profile uses the namespace of that Workspace
// only. A separated namespace can never resolve to the main one.
class SyncNamespace {
 public:
  static SyncNamespace Main();
  // Nullopt for an invalid Workspace id: the caller must then not sync.
  static std::optional<SyncNamespace> ForSeparatedWorkspace(
      const base::Uuid& workspace_id);

  SyncNamespace(const SyncNamespace&);
  SyncNamespace& operator=(const SyncNamespace&);
  ~SyncNamespace();

  bool is_main() const { return !workspace_id_.is_valid(); }
  // Invalid for the main namespace.
  const base::Uuid& workspace_id() const { return workspace_id_; }

  bool operator==(const SyncNamespace&) const = default;

 private:
  explicit SyncNamespace(base::Uuid workspace_id);

  base::Uuid workspace_id_;
};

// The per-namespace CloudKit and Keychain identifiers. `base` carries the
// bundle-configured main values.
struct SyncNamespaceIdentifiers {
  std::string zone_name;
  // Empty means CKSyncEngine's default subscription id.
  std::string subscription_identifier;
  std::string keychain_account;

  bool operator==(const SyncNamespaceIdentifiers&) const = default;
};

bool IsValidCloudKitZoneName(std::string_view zone_name);

// Main namespace: returns `base` unchanged, byte for byte. Separated
// namespace: zone `<base zone>-ws-<uuid>`, subscription
// `<base subscription>-ws-<uuid>` (or the zone name when the base is empty,
// so two sync engines on the same database never share CKSyncEngine's default
// subscription id), key account `<base account>.ws-<uuid>`. Nullopt when a
// base value it derives from is empty or the derived zone name is invalid;
// the caller must then create no provider.
std::optional<SyncNamespaceIdentifiers> ResolveSyncNamespace(
    const SyncNamespace& sync_namespace,
    const SyncNamespaceIdentifiers& base);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_NAMESPACE_H_
