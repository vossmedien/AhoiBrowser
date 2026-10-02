// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CRYPTO_APPLE_AHOI_KEYCHAIN_SCOPE_MAC_H_
#define CRYPTO_APPLE_AHOI_KEYCHAIN_SCOPE_MAC_H_

#include <Security/Security.h>
#include <string_view>

#include "base/apple/scoped_cftyperef.h"
#include "crypto/crypto_export.h"

namespace crypto::apple {

CRYPTO_EXPORT bool IsAhoiSafeStorageFallbackAllowed(
    std::string_view service,
    std::string_view account,
    std::string_view process_home,
    std::string_view account_home,
    OSStatus default_status,
    bool writing);

// Selects only an existing, UID-owned login keychain when a relocated process
// home has no native default. Never changes preferences, trust, ACLs or HOME.
CRYPTO_EXPORT base::apple::ScopedCFTypeRef<SecKeychainRef>
SelectAhoiSafeStorageFallback(std::string_view service,
                             std::string_view account,
                             bool writing);

}  // namespace crypto::apple
#endif  // CRYPTO_APPLE_AHOI_KEYCHAIN_SCOPE_MAC_H_
