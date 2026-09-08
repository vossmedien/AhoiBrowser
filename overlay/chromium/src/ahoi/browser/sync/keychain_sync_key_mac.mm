// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/keychain_sync_key_mac.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <utility>
#include <vector>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "base/apple/foundation_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"

namespace ahoi::sync {

std::unique_ptr<SyncPayloadCryptor> LoadKeychainSyncPayloadCryptor(
    const CloudKitSyncConfigurationMac& configuration) {
  if (!configuration.IsE2EKeyConfigured()) {
    return nullptr;
  }
  NSMutableDictionary* query = [@{
    (__bridge NSString*)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge NSString*)kSecAttrService :
        [NSString stringWithUTF8String:configuration.keychain_service.c_str()],
    (__bridge NSString*)kSecAttrAccount :
        [NSString stringWithUTF8String:configuration.keychain_account.c_str()],
    (__bridge NSString*)kSecAttrSynchronizable : @YES,
    (__bridge NSString*)kSecUseDataProtectionKeychain : @YES,
    (__bridge NSString*)kSecReturnData : @YES,
    (__bridge NSString*)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
  } mutableCopy];
  if (!configuration.keychain_access_group.empty()) {
    query[(__bridge NSString*)kSecAttrAccessGroup] = [NSString
        stringWithUTF8String:configuration.keychain_access_group.c_str()];
  }

  CFTypeRef result = nullptr;
  const OSStatus status =
      SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status != errSecSuccess || !result) {
    if (result) {
      CFRelease(result);
    }
    return nullptr;
  }
  NSData* data = CFBridgingRelease(result);
  if (![data isKindOfClass:[NSData class]] || data.length != 32) {
    return nullptr;
  }
  if (!configuration.verified_key_sha256.empty() &&
      base::ToLowerASCII(base::HexEncode(
          crypto::SHA256Hash(base::apple::NSDataToSpan(data)))) !=
          configuration.verified_key_sha256) {
    return nullptr;  // Bind the actual loaded bytes, not an earlier Keychain
                     // read.
  }
  std::vector<uint8_t> key(data.length);
  [data getBytes:key.data() length:key.size()];
  return std::make_unique<Aes256GcmSyncPayloadCryptor>(
      std::move(key), configuration.key_version);
}

}  // namespace ahoi::sync
