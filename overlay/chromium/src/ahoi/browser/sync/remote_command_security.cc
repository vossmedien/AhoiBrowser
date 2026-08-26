// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/remote_command_security.h"

#include <cstdint>
#include <string_view>

#include "ahoi/browser/sync/sync_merge.h"
#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace ahoi::sync {
namespace {

constexpr base::TimeDelta kAllowedFutureClockSkew = base::Minutes(1);
constexpr int64_t kWindowsToUnixEpochMicros = 11644473600000000LL;

std::string JsonString(std::string_view value) {
  std::string result;
  if (!base::JSONWriter::Write(base::Value(value), &result)) {
    return {};
  }
  return result;
}

std::string UuidJson(const base::Uuid& value) {
  return JsonString(base::ToUpperASCII(value.AsLowercaseString()));
}

std::string IdentifierJson(const base::Uuid& value) {
  return base::StrCat({"{\"rawValue\":", UuidJson(value), "}"});
}

std::string CommandJson(const RemoteCommandRecord& command) {
  switch (command.kind) {
    case RemoteCommandKind::kOpen: {
      std::string request = base::StrCat({"{\"url\":", JsonString(command.url)});
      if (command.workspace_id) {
        request = base::StrCat(
            {request, ",\"workspaceID\":", IdentifierJson(*command.workspace_id)});
      }
      return base::StrCat(
          {"{\"kind\":\"open\",\"openRequest\":", request, "}}"});
    }
    case RemoteCommandKind::kFocus:
      return base::StrCat(
          {"{\"kind\":\"focus\",\"tabReference\":{\"context\":"
           "\"normal\",\"tabID\":",
           IdentifierJson(*command.tab_id), "}}"});
    case RemoteCommandKind::kClose:
      return base::StrCat(
          {"{\"kind\":\"close\",\"tabReferences\":[{\"context\":"
           "\"normal\",\"tabID\":",
           IdentifierJson(*command.tab_id), "}]}"});
  }
}

}  // namespace

bool CanonicalRemoteCommandPayload(const RemoteCommandRecord& command,
                                   std::string* payload) {
  if (!payload || !command.id.is_valid() ||
      !command.source_device_id.is_valid() ||
      !command.target_device_id.is_valid() ||
      ((command.kind == RemoteCommandKind::kFocus ||
        command.kind == RemoteCommandKind::kClose) &&
       !command.tab_id)) {
    return false;
  }
  const int64_t windows_micros =
      command.issued_at.ToDeltaSinceWindowsEpoch().InMicroseconds();
  if (windows_micros < kWindowsToUnixEpochMicros) {
    return false;
  }
  const int64_t issued_millis =
      (windows_micros - kWindowsToUnixEpochMicros) / 1000;
  const std::string command_json = CommandJson(command);
  if (command_json.empty()) {
    return false;
  }
  *payload = base::StrCat(
      {"{\"command\":", command_json, ",\"commandID\":", UuidJson(command.id),
       ",\"issuedAtMilliseconds\":", base::NumberToString(issued_millis),
       ",\"nonce\":", JsonString(command.nonce_base64),
       ",\"sourceDeviceID\":", IdentifierJson(command.source_device_id),
       ",\"targetDeviceID\":", IdentifierJson(command.target_device_id), "}"});
  return true;
}

RemoteCommandValidationFailure ValidateRemoteCommandForExecution(
    const RemoteCommandRecord& command,
    const base::Uuid& local_device_id,
    const RemoteCommandPolicy& policy,
    base::Time now) {
  if (!ValidateRecord(command, nullptr) ||
      command.status != RemoteCommandStatus::kQueued) {
    return RemoteCommandValidationFailure::kInvalidPayload;
  }
  if (command.target_device_id != local_device_id) {
    return RemoteCommandValidationFailure::kWrongTarget;
  }
  if (!policy.enabled) {
    return RemoteCommandValidationFailure::kDisabled;
  }
  if (command.issued_at > now + kAllowedFutureClockSkew) {
    return RemoteCommandValidationFailure::kIssuedInFuture;
  }
  if (now >= command.expires_at) {
    return RemoteCommandValidationFailure::kExpired;
  }
  auto approved = policy.approved_public_keys_base64.find(
      command.source_device_id);
  if (approved == policy.approved_public_keys_base64.end()) {
    return RemoteCommandValidationFailure::kUnapprovedDevice;
  }
  std::string public_key;
  std::string signature;
  std::string signed_payload;
  if (!base::Base64Decode(approved->second, &public_key) ||
      public_key.size() != ED25519_PUBLIC_KEY_LEN ||
      !base::Base64Decode(command.signature_base64, &signature) ||
      signature.size() != ED25519_SIGNATURE_LEN ||
      !CanonicalRemoteCommandPayload(command, &signed_payload)) {
    return RemoteCommandValidationFailure::kInvalidSignature;
  }
  if (ED25519_verify(
          reinterpret_cast<const uint8_t*>(signed_payload.data()),
          signed_payload.size(),
          reinterpret_cast<const uint8_t*>(signature.data()),
          reinterpret_cast<const uint8_t*>(public_key.data())) != 1) {
    return RemoteCommandValidationFailure::kInvalidSignature;
  }
  return RemoteCommandValidationFailure::kNone;
}

const char* SafeRemoteCommandFailureCode(
    RemoteCommandValidationFailure failure) {
  switch (failure) {
    case RemoteCommandValidationFailure::kNone:
      return "";
    case RemoteCommandValidationFailure::kWrongTarget:
      return "wrong_target";
    case RemoteCommandValidationFailure::kDisabled:
      return "policy_rejected";
    case RemoteCommandValidationFailure::kExpired:
      return "expired";
    case RemoteCommandValidationFailure::kIssuedInFuture:
      return "clock_rejected";
    case RemoteCommandValidationFailure::kUnapprovedDevice:
      return "policy_rejected";
    case RemoteCommandValidationFailure::kInvalidSignature:
      return "invalid_signature";
    case RemoteCommandValidationFailure::kInvalidPayload:
      return "invalid_command";
  }
}

}  // namespace ahoi::sync
