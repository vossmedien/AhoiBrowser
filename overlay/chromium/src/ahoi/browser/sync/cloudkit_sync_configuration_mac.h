// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_

#include <cstdint>
#include <optional>
#include <string>

namespace ahoi::sync {

struct CloudKitSyncConfigurationMac {
  std::string container_identifier;
  std::string zone_name = "AhoiBrowserSyncZone";
  std::string subscription_identifier;
  std::string keychain_service;
  std::string keychain_account;
  std::string keychain_access_group;
  uint32_t key_version = 0;
  bool automatically_sync = true;

  bool IsTransportConfigured() const;
  bool IsE2EKeyConfigured() const;

  // Reads only fork-configurable Info.plist values. Unresolved build-setting
  // placeholders are treated as absent, so an unsigned local build is inert.
  static std::optional<CloudKitSyncConfigurationMac> FromMainBundle();
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_CONFIGURATION_MAC_H_
