// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_secret_store.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/containers/span.h"
#include "base/strings/sys_string_conversions.h"
#include "crypto/apple/keychain_v2.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

using base::apple::CFToNSPtrCast;
using base::apple::NSToCFPtrCast;

constexpr char kExpectedService[] =
    "org.ahoibrowser.developer-toolkit.header-secrets";
constexpr char kTestAccessGroup[] = "org.ahoibrowser.tests.developer-toolkit";

class InMemoryDeveloperKeychain final : public crypto::apple::KeychainV2 {
 public:
  InMemoryDeveloperKeychain() = default;
  ~InMemoryDeveloperKeychain() override = default;

  OSStatus ItemAdd(CFDictionaryRef attributes, CFTypeRef* result) override {
    if (add_status_ != errSecSuccess) {
      return add_status_;
    }
    NSDictionary* dictionary = CFToNSPtrCast(attributes);
    NSString* account = dictionary[CFToNSPtrCast(kSecAttrAccount)];
    NSString* service = dictionary[CFToNSPtrCast(kSecAttrService)];
    NSString* access_group = dictionary[CFToNSPtrCast(kSecAttrAccessGroup)];
    NSData* secret = dictionary[CFToNSPtrCast(kSecValueData)];
    if (!account || !service || !secret ||
        ![access_group
            isEqualToString:base::SysUTF8ToNSString(kTestAccessGroup)]) {
      return errSecParam;
    }

    const std::string account_string = base::SysNSStringToUTF8(account);
    if (items_.contains(account_string)) {
      return errSecDuplicateItem;
    }
    const base::span<const uint8_t> bytes = base::apple::NSDataToSpan(secret);
    items_.emplace(account_string,
                   std::vector<uint8_t>(bytes.begin(), bytes.end()));
    last_service_ = base::SysNSStringToUTF8(service);
    last_access_group_ = base::SysNSStringToUTF8(access_group);
    last_synchronizable_was_local_ =
        [dictionary[CFToNSPtrCast(kSecAttrSynchronizable)] isEqual:@NO];
    last_accessibility_was_device_local_ =
        [dictionary[CFToNSPtrCast(kSecAttrAccessible)]
            isEqual:CFToNSPtrCast(
                        kSecAttrAccessibleWhenUnlockedThisDeviceOnly)];
    if (result) {
      *result = nullptr;
    }
    return errSecSuccess;
  }

  OSStatus ItemCopyMatching(CFDictionaryRef query, CFTypeRef* result) override {
    if (copy_status_ != errSecSuccess) {
      return copy_status_;
    }
    NSDictionary* dictionary = CFToNSPtrCast(query);
    NSString* account = dictionary[CFToNSPtrCast(kSecAttrAccount)];
    if (!account) {
      return errSecParam;
    }
    const auto found = items_.find(base::SysNSStringToUTF8(account));
    if (found == items_.end()) {
      return errSecItemNotFound;
    }
    if (result) {
      NSData* data = [NSData dataWithBytes:found->second.data()
                                    length:found->second.size()];
      *result = CFRetain(NSToCFPtrCast(data));
    }
    return errSecSuccess;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    if (delete_status_ != errSecSuccess) {
      return delete_status_;
    }
    NSDictionary* dictionary = CFToNSPtrCast(query);
    NSString* account = dictionary[CFToNSPtrCast(kSecAttrAccount)];
    if (!account) {
      return errSecParam;
    }
    return items_.erase(base::SysNSStringToUTF8(account)) == 1
               ? errSecSuccess
               : errSecItemNotFound;
  }

  void set_add_status(OSStatus status) { add_status_ = status; }
  void set_copy_status(OSStatus status) { copy_status_ = status; }
  void set_delete_status(OSStatus status) { delete_status_ = status; }

  const std::string& last_service() const { return last_service_; }
  const std::string& last_access_group() const { return last_access_group_; }
  bool last_synchronizable_was_local() const {
    return last_synchronizable_was_local_;
  }
  bool last_accessibility_was_device_local() const {
    return last_accessibility_was_device_local_;
  }
  size_t item_count() const { return items_.size(); }

 private:
  std::map<std::string, std::vector<uint8_t>> items_;
  OSStatus add_status_ = errSecSuccess;
  OSStatus copy_status_ = errSecSuccess;
  OSStatus delete_status_ = errSecSuccess;
  std::string last_service_;
  std::string last_access_group_;
  bool last_synchronizable_was_local_ = false;
  bool last_accessibility_was_device_local_ = false;
};

TEST(DeveloperSecretStoreMacTest, StoresOnlyOpaqueLocalKeychainReference) {
  InMemoryDeveloperKeychain keychain;
  std::unique_ptr<DeveloperSecretStore> store =
      CreateMacDeveloperSecretStoreForTesting(&keychain, kTestAccessGroup);
  ASSERT_TRUE(store);

  constexpr std::string_view kSecret = "Bearer top-secret";
  const std::optional<std::string> reference =
      store->Store("Authorization test token", kSecret);
  ASSERT_TRUE(reference);
  EXPECT_TRUE(reference->starts_with("ahoi-keychain:"));
  EXPECT_EQ(reference->find(kSecret), std::string::npos);
  EXPECT_EQ(keychain.last_service(), kExpectedService);
  EXPECT_EQ(keychain.last_access_group(), kTestAccessGroup);
  EXPECT_TRUE(keychain.last_synchronizable_was_local());
  EXPECT_TRUE(keychain.last_accessibility_was_device_local());
  EXPECT_EQ(keychain.item_count(), 1u);

  const std::optional<std::string> resolved = store->Resolve(*reference);
  ASSERT_TRUE(resolved);
  EXPECT_EQ(*resolved, kSecret);
  EXPECT_TRUE(store->Remove(*reference));
  EXPECT_FALSE(store->Resolve(*reference));
  EXPECT_EQ(keychain.item_count(), 0u);
}

TEST(DeveloperSecretStoreMacTest, InvalidInputAndKeychainErrorsFailClosed) {
  InMemoryDeveloperKeychain keychain;
  std::unique_ptr<DeveloperSecretStore> store =
      CreateMacDeveloperSecretStoreForTesting(&keychain, kTestAccessGroup);
  ASSERT_TRUE(store);

  EXPECT_FALSE(store->Store("", "secret"));
  EXPECT_FALSE(store->Store("token", "line one\r\nline two"));
  EXPECT_EQ(keychain.item_count(), 0u);
  EXPECT_FALSE(
      CreateMacDeveloperSecretStoreForTesting(nullptr, kTestAccessGroup));

  keychain.set_add_status(errSecAuthFailed);
  EXPECT_FALSE(store->Store("token", "secret"));
  keychain.set_add_status(errSecSuccess);
  const std::optional<std::string> reference = store->Store("token", "secret");
  ASSERT_TRUE(reference);

  keychain.set_copy_status(errSecInteractionNotAllowed);
  EXPECT_FALSE(store->Resolve(*reference));
  keychain.set_copy_status(errSecSuccess);
  keychain.set_delete_status(errSecInteractionNotAllowed);
  EXPECT_FALSE(store->Remove(*reference));
  EXPECT_FALSE(store->Resolve("not-an-ahoi-keychain-reference"));
}

}  // namespace
}  // namespace ahoi
