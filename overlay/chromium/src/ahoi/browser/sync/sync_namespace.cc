// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_namespace.h"

#include <utility>

#include "base/strings/string_util.h"

namespace ahoi::sync {

SyncNamespace SyncNamespace::Main() {
  return SyncNamespace(base::Uuid());
}

std::optional<SyncNamespace> SyncNamespace::ForSeparatedWorkspace(
    const base::Uuid& workspace_id) {
  if (!workspace_id.is_valid()) {
    return std::nullopt;
  }
  return SyncNamespace(workspace_id);
}

SyncNamespace::SyncNamespace(base::Uuid workspace_id)
    : workspace_id_(std::move(workspace_id)) {}
SyncNamespace::SyncNamespace(const SyncNamespace&) = default;
SyncNamespace& SyncNamespace::operator=(const SyncNamespace&) = default;
SyncNamespace::~SyncNamespace() = default;

bool IsValidCloudKitZoneName(std::string_view zone_name) {
  if (zone_name.empty() || zone_name.size() > kMaxCloudKitZoneNameLength ||
      zone_name.front() == '_') {
    return false;
  }
  for (const char c : zone_name) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

std::optional<SyncNamespaceIdentifiers> ResolveSyncNamespace(
    const SyncNamespace& sync_namespace,
    const SyncNamespaceIdentifiers& base) {
  if (sync_namespace.is_main()) {
    return base;  // Unchanged; the main Profile's behaviour stays identical.
  }
  // A base that already looks like a Workspace zone would make the main zone
  // indistinguishable from a separated one for the Companion's prefix scan.
  if (base.zone_name.empty() || base.keychain_account.empty() ||
      base.zone_name.find(kWorkspaceZoneInfix) != std::string::npos ||
      base.keychain_account.find(kWorkspaceKeyAccountInfix) !=
          std::string::npos) {
    return std::nullopt;
  }
  const std::string id = sync_namespace.workspace_id().AsLowercaseString();
  SyncNamespaceIdentifiers result;
  result.zone_name = base.zone_name + kWorkspaceZoneInfix + id;
  result.subscription_identifier =
      base.subscription_identifier.empty()
          ? result.zone_name
          : base.subscription_identifier + kWorkspaceZoneInfix + id;
  result.keychain_account = base.keychain_account + kWorkspaceKeyAccountInfix +
                            id;
  if (!IsValidCloudKitZoneName(result.zone_name) ||
      result.zone_name == base.zone_name ||
      result.subscription_identifier == base.subscription_identifier ||
      result.keychain_account == base.keychain_account) {
    return std::nullopt;
  }
  return result;
}

}  // namespace ahoi::sync
