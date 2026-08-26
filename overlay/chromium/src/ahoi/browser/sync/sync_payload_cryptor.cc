// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_payload_cryptor.h"

#include <utility>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/values.h"
#include "crypto/aead.h"

namespace ahoi::sync {
namespace {

constexpr size_t kKeySize = 32;
constexpr size_t kNonceSize = 12;
constexpr char kAlgorithm[] = "AES-256-GCM";

}  // namespace

Aes256GcmSyncPayloadCryptor::Aes256GcmSyncPayloadCryptor(
    std::vector<uint8_t> key,
    uint32_t key_version)
    : key_(std::move(key)), key_version_(key_version) {}

Aes256GcmSyncPayloadCryptor::~Aes256GcmSyncPayloadCryptor() {
  std::fill(key_.begin(), key_.end(), 0);
}

std::optional<std::string> Aes256GcmSyncPayloadCryptor::Seal(
    std::string_view plaintext) {
  if (key_.size() != kKeySize || key_version_ == 0) {
    return std::nullopt;
  }
  std::vector<uint8_t> nonce(kNonceSize);
  base::RandBytes(nonce);
  const std::vector<uint8_t> ciphertext = crypto::aead::Seal(
      crypto::aead::AES_256_GCM, key_, base::as_byte_span(plaintext), nonce,
      base::span<const uint8_t>());
  if (ciphertext.size() < 16) {
    return std::nullopt;
  }

  base::DictValue envelope;
  envelope.Set("algorithm", kAlgorithm);
  envelope.Set("ciphertextAndTag", base::Base64Encode(ciphertext));
  envelope.Set("keyVersion", static_cast<int>(key_version_));
  envelope.Set("nonce", base::Base64Encode(nonce));
  std::string encoded;
  if (!base::JSONWriter::Write(base::Value(std::move(envelope)), &encoded)) {
    return std::nullopt;
  }
  return encoded;
}

std::optional<std::string> Aes256GcmSyncPayloadCryptor::Open(
    std::string_view envelope_json) {
  if (key_.size() != kKeySize || key_version_ == 0) {
    return std::nullopt;
  }
  std::optional<base::DictValue> envelope =
      base::JSONReader::ReadDict(envelope_json, base::JSON_PARSE_RFC);
  const std::string* algorithm =
      envelope ? envelope->FindString("algorithm") : nullptr;
  if (!envelope || !algorithm || *algorithm != kAlgorithm ||
      envelope->FindInt("keyVersion").value_or(0) !=
          static_cast<int>(key_version_)) {
    return std::nullopt;
  }
  const std::string* encoded_nonce = envelope->FindString("nonce");
  const std::string* encoded_ciphertext =
      envelope->FindString("ciphertextAndTag");
  std::optional<std::vector<uint8_t>> nonce =
      encoded_nonce ? base::Base64Decode(*encoded_nonce) : std::nullopt;
  std::optional<std::vector<uint8_t>> ciphertext =
      encoded_ciphertext ? base::Base64Decode(*encoded_ciphertext)
                         : std::nullopt;
  if (!nonce || nonce->size() != kNonceSize || !ciphertext ||
      ciphertext->size() < 16) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> plaintext = crypto::aead::Open(
      crypto::aead::AES_256_GCM, key_, *ciphertext, *nonce,
      base::span<const uint8_t>());
  if (!plaintext) {
    return std::nullopt;
  }
  return std::string(base::as_string_view(*plaintext));
}

}  // namespace ahoi::sync
