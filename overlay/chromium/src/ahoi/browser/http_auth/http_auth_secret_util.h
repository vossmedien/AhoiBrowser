// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_UTIL_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_UTIL_H_

#include <string>

namespace ahoi {

// Best-effort explicit zeroing for transient plaintext owned by Ahoi. This
// delegates to Chromium's platform-backed crypto utility; it is not encryption
// or an additional credential store.
void SecurelyClearHttpAuthSecret(std::u16string* secret);

// Move-only transient plaintext whose backing string is explicitly zeroed on
// replacement and destruction. It is used across asynchronous callbacks so a
// canceled callback does not leave an ordinary string allocation behind.
class ScopedHttpAuthSecret {
 public:
  explicit ScopedHttpAuthSecret(std::u16string secret);
  ScopedHttpAuthSecret(ScopedHttpAuthSecret&& other) noexcept;
  ScopedHttpAuthSecret& operator=(ScopedHttpAuthSecret&& other) noexcept;
  ScopedHttpAuthSecret(const ScopedHttpAuthSecret&) = delete;
  ScopedHttpAuthSecret& operator=(const ScopedHttpAuthSecret&) = delete;
  ~ScopedHttpAuthSecret();

  const std::u16string& value() const { return secret_; }
  bool empty() const { return secret_.empty(); }
  void Clear();

 private:
  std::u16string secret_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_UTIL_H_
