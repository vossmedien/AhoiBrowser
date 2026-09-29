// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_BOOTSTRAP_MAC_H_
#define AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_BOOTSTRAP_MAC_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/sync_authorization.h"

namespace ahoi::sync {

struct CloudKitSyncConfigurationMac;

enum class KeychainBootstrapResult {
  kOk,
  kMissing,
  kCancelled,
  kUnavailable,
  kCorrupt,
  kConflict,
};

// Local metadata only. Hashes are lowercase SHA256 of an actual validated
// 32-byte key, not an assertion that the server accepted that key. The caller
// must independently compare the current server claim's key commitment.
struct KeychainBootstrapState {
  bool canonical_key_present = false;
  // A valid pending item or a prepared journal backed by its actual key.
  bool candidate_present = false;
  bool uses_existing_key = false;
  std::optional<std::string> accepted_change_tag;
  std::optional<std::string> canonical_sha256;
  std::optional<std::string> candidate_sha256;
};

// Synchronous Keychain primitives matching CompanionPayloadKeyStore.swift.
// The caller MUST retain an exclusive cross-process lock for this complete
// service/account/access-group/key-version family across the whole asynchronous
// server-claim workflow, including calls below. This object acquires neither a
// new authorization nor a CloudKit claim, and destruction never deletes keys.
class KeychainSyncBootstrapMac final {
 public:
  KeychainSyncBootstrapMac(const CloudKitSyncConfigurationMac& configuration,
                           SyncAuthorization authorization);
  ~KeychainSyncBootstrapMac();
  KeychainSyncBootstrapMac(const KeychainSyncBootstrapMac&) = delete;
  KeychainSyncBootstrapMac& operator=(const KeychainSyncBootstrapMac&) = delete;

  // kMissing means all three exact items are absent, with an empty state.
  // Every error clears the output; OS failures never masquerade as absence.
  KeychainBootstrapResult ReadState(KeychainBootstrapState* state);
  KeychainBootstrapResult PrepareCandidate(KeychainBootstrapState* state);
  KeychainBootstrapResult RecordAcceptedClaim(std::string_view change_tag);
  KeychainBootstrapResult PromoteAcceptedCandidate(
      std::string_view matching_change_tag);
  KeychainBootstrapResult DiscardUnacceptedCandidate();

 private:
  class Impl;
  const std::unique_ptr<Impl> impl_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_KEYCHAIN_SYNC_BOOTSTRAP_MAC_H_
