// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_

#include <cstdint>
#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_authorization.h"

namespace ahoi::sync {

struct CloudKitSyncConfigurationMac {
  std::string container_identifier;
  std::string zone_name = "AhoiBrowserSyncV3";
  std::string subscription_identifier;
  std::string keychain_service;
  std::string keychain_account;
  std::string keychain_access_group;
  uint32_t key_version = 0;
  // Filled only by the verified native bootstrap, never from Info.plist.
  std::string verified_key_sha256;
  // In-memory identity from the bootstrap's matching before/after account
  // reads. Never loaded from Info.plist, persisted, logged, or sent on the
  // wire.
  std::string verified_account_record_name;
  SyncAuthorization verified_key_authorization;
  bool automatically_sync = true;

  bool IsTransportConfigured() const;
  bool IsE2EKeyConfigured() const;

  // Reads only fork-configurable Info.plist values. Unresolved build-setting
  // placeholders are treated as absent, so an unsigned local build is inert.
  static std::optional<CloudKitSyncConfigurationMac> FromMainBundle();
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_
