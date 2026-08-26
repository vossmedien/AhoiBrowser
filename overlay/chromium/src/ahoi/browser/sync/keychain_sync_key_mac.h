// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_KEY_MAC_H_
#define AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_KEY_MAC_H_

#include <memory>

namespace ahoi::sync {

struct CloudKitSyncConfigurationMac;
class SyncPayloadCryptor;

// Returns null unless a 32-byte synchronizable data-protection Keychain item
// has already been provisioned by the approved product key lifecycle.
std::unique_ptr<SyncPayloadCryptor> LoadKeychainSyncPayloadCryptor(
    const CloudKitSyncConfigurationMac& configuration);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_KEY_MAC_H_
