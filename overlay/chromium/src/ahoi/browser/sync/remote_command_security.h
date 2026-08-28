// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_REMOTE_COMMAND_SECURITY_H_
#define AHOI_BROWSER_SYNC_REMOTE_COMMAND_SECURITY_H_

#include <map>
#include <string>

#include "ahoi/browser/sync/sync_model.h"

namespace ahoi::sync {

struct RemoteCommandPolicy {
  // Explicitly false unless the user enables remote control in this profile.
  bool enabled = false;
  // Device UUID -> raw 32-byte Ed25519 public key encoded as standard base64.
  // Approval and key provisioning are local profile state, never CloudKit
  // payloads.
  std::map<base::Uuid, std::string> approved_public_keys_base64;
};

// Validates the complete local approval credential, not merely that a value
// exists in Preferences. A verified sender key is exactly one raw 32-byte
// Ed25519 public key encoded as standard Base64.
[[nodiscard]] bool IsValidRemoteControlPublicKeyBase64(
    const std::string& public_key_base64);

enum class RemoteCommandValidationFailure {
  kNone = 0,
  kWrongTarget,
  kDisabled,
  kExpired,
  kIssuedInFuture,
  kUnapprovedDevice,
  kInvalidSignature,
  kInvalidPayload,
};

// Produces the exact sorted-key JSON signed by the Swift companion. Status,
// result and HLC are intentionally excluded because the target owns them.
[[nodiscard]] bool CanonicalRemoteCommandPayload(
    const RemoteCommandRecord& command,
    std::string* payload);

[[nodiscard]] RemoteCommandValidationFailure ValidateRemoteCommandForExecution(
    const RemoteCommandRecord& command,
    const base::Uuid& local_device_id,
    const RemoteCommandPolicy& policy,
    base::Time now);

const char* SafeRemoteCommandFailureCode(
    RemoteCommandValidationFailure failure);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_REMOTE_COMMAND_SECURITY_H_
