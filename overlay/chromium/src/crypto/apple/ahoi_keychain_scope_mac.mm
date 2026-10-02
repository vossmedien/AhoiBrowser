// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crypto/apple/ahoi_keychain_scope_mac.h"

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cstdlib>
#include <string>

namespace crypto::apple {
namespace {

// Existing Safe Storage belongs to a file keychain. macOS supplies these
// deprecated file-selection APIs only; moving to a data-protection store would
// change the encryption-key owner. Keep suppression local to these two calls.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
OSStatus CopyNativeDefault(SecKeychainRef* keychain) {
  return SecKeychainCopyDomainDefault(kSecPreferencesDomainUser, keychain);
}
OSStatus OpenExistingFile(const char* path, SecKeychainRef* keychain) {
  return SecKeychainOpen(path, keychain);
}
#pragma clang diagnostic pop

}  // namespace

bool IsAhoiSafeStorageFallbackAllowed(std::string_view service,
                                     std::string_view account,
                                     std::string_view process_home,
                                     std::string_view account_home,
                                     OSStatus default_status,
                                     bool writing) {
  const bool own = service == "Ahoi Safe Storage" && account == "Ahoi";
  const bool legacy_read = !writing && service == "Chromium Safe Storage" &&
                           account == "Chromium";
  return (own || legacy_read) && default_status == errSecNoDefaultKeychain &&
         !process_home.empty() && !account_home.empty() &&
         process_home.front() == '/' && account_home.front() == '/' &&
         process_home != account_home;
}

base::apple::ScopedCFTypeRef<SecKeychainRef> SelectAhoiSafeStorageFallback(
    std::string_view service,
    std::string_view account,
    bool writing) {
  // Avoid even a default-keychain probe for unrelated consumers.
  if (!((service == "Ahoi Safe Storage" && account == "Ahoi") ||
        (!writing && service == "Chromium Safe Storage" && account == "Chromium"))) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  const char* process_home = std::getenv("HOME");
  struct passwd entry;
  struct passwd* resolved = nullptr;
  std::array<char, 16384> buffer;
  if (!process_home || getuid() != geteuid() ||
      getpwuid_r(geteuid(), &entry, buffer.data(), buffer.size(), &resolved) != 0 ||
      !resolved || !entry.pw_dir) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  base::apple::ScopedCFTypeRef<SecKeychainRef> native_default;
  const OSStatus status = CopyNativeDefault(native_default.InitializeInto());
  if (!IsAhoiSafeStorageFallbackAllowed(service, account, process_home,
                                        entry.pw_dir, status, writing)) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  const std::string path = std::string(entry.pw_dir) +
                           "/Library/Keychains/login.keychain-db";
  struct stat file;
  if (lstat(path.c_str(), &file) != 0 || !S_ISREG(file.st_mode) ||
      file.st_uid != geteuid()) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  base::apple::ScopedCFTypeRef<SecKeychainRef> keychain;
  if (OpenExistingFile(path.c_str(), keychain.InitializeInto()) != errSecSuccess) {
    return base::apple::ScopedCFTypeRef<SecKeychainRef>();
  }
  // A locked keychain stays locked: Security.framework owns authentication.
  return keychain;
}

}  // namespace crypto::apple
