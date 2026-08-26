// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_SYNC_PAYLOAD_CRYPTOR_H_
#define AHOI_BROWSER_SYNC_SYNC_PAYLOAD_CRYPTOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"

namespace ahoi::sync {

// Application-layer cryptography is isolated from CloudKit transport. The
// implementation delegates AES-GCM to Chromium/BoringSSL and accepts only an
// externally provisioned 256-bit key; it never derives or generates a sync
// master key.
class SyncPayloadCryptor {
 public:
  virtual ~SyncPayloadCryptor() = default;
  virtual std::optional<std::string> Seal(std::string_view plaintext) = 0;
  virtual std::optional<std::string> Open(std::string_view envelope_json) = 0;
};

class Aes256GcmSyncPayloadCryptor final : public SyncPayloadCryptor {
 public:
  Aes256GcmSyncPayloadCryptor(std::vector<uint8_t> key,
                             uint32_t key_version);
  ~Aes256GcmSyncPayloadCryptor() override;

  std::optional<std::string> Seal(std::string_view plaintext) override;
  std::optional<std::string> Open(std::string_view envelope_json) override;

 private:
  std::vector<uint8_t> key_;
  const uint32_t key_version_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_PAYLOAD_CRYPTOR_H_
