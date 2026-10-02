// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crypto/apple/ahoi_keychain_scope_mac.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace crypto::apple {
namespace {

TEST(AhoiKeychainScopeTest, OwnStorageOnlyWithMissingDefaultAndRelocatedHome) {
  EXPECT_TRUE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, false));
  EXPECT_TRUE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, true));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "/Users/person", "/Users/person",
      errSecNoDefaultKeychain, false));
}

TEST(AhoiKeychainScopeTest, ExistingDefaultAndOtherErrorsRemainAuthoritative) {
  for (const OSStatus status : {errSecSuccess, errSecAuthFailed, errSecNotAvailable}) {
    EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
        "Ahoi Safe Storage", "Ahoi", "/isolated/account", "/Users/person",
        status, false));
  }
}

TEST(AhoiKeychainScopeTest, LegacyStorageIsReadOnlyAndOtherItemsStayUnchanged) {
  EXPECT_TRUE(IsAhoiSafeStorageFallbackAllowed(
      "Chromium Safe Storage", "Chromium", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, false));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Chromium Safe Storage", "Chromium", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, true));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Chrome Safe Storage", "Chrome", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, false));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "different-account", "/isolated/account", "/Users/person",
      errSecNoDefaultKeychain, false));
}

TEST(AhoiKeychainScopeTest, MissingOrRelativeAccountIdentityFailsClosed) {
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "", "/Users/person",
      errSecNoDefaultKeychain, false));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "/isolated/account", "relative",
      errSecNoDefaultKeychain, false));
  EXPECT_FALSE(IsAhoiSafeStorageFallbackAllowed(
      "Ahoi Safe Storage", "Ahoi", "relative", "/Users/person",
      errSecNoDefaultKeychain, false));
}

}  // namespace
}  // namespace crypto::apple
