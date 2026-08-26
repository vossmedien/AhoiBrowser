// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_secret_util.h"

#include <utility>

#include "base/containers/span.h"
#include "crypto/secure_util.h"

namespace ahoi {

void SecurelyClearHttpAuthSecret(std::u16string* secret) {
  if (!secret) {
    return;
  }
  if (!secret->empty()) {
    crypto::SecureZeroBuffer(base::as_writable_byte_span(*secret));
  }
  secret->clear();
}

ScopedHttpAuthSecret::ScopedHttpAuthSecret(std::u16string secret)
    : secret_(std::move(secret)) {
  SecurelyClearHttpAuthSecret(&secret);
}

ScopedHttpAuthSecret::ScopedHttpAuthSecret(
    ScopedHttpAuthSecret&& other) noexcept
    : secret_(std::move(other.secret_)) {
  SecurelyClearHttpAuthSecret(&other.secret_);
}

ScopedHttpAuthSecret& ScopedHttpAuthSecret::operator=(
    ScopedHttpAuthSecret&& other) noexcept {
  if (this != &other) {
    SecurelyClearHttpAuthSecret(&secret_);
    secret_ = std::move(other.secret_);
    SecurelyClearHttpAuthSecret(&other.secret_);
  }
  return *this;
}

ScopedHttpAuthSecret::~ScopedHttpAuthSecret() {
  SecurelyClearHttpAuthSecret(&secret_);
}

void ScopedHttpAuthSecret::Clear() {
  SecurelyClearHttpAuthSecret(&secret_);
}

}  // namespace ahoi
