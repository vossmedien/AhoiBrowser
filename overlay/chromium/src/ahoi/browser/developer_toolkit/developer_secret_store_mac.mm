// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_secret_store.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/uuid.h"
#include "crypto/apple/keychain_v2.h"

namespace ahoi {
namespace {

using base::apple::CFToNSPtrCast;
using base::apple::NSToCFPtrCast;

constexpr char kKeychainService[] =
    "org.ahoibrowser.developer-toolkit.header-secrets";
constexpr size_t kMaxSecretLabelBytes = 96;
constexpr int kReferenceCreationAttempts = 3;

bool IsValidLabel(std::string_view label) {
  return !label.empty() && label.size() <= kMaxSecretLabelBytes &&
         base::IsStringUTF8(label) &&
         label.find('\0') == std::string_view::npos;
}

std::optional<std::string> AccountForReference(std::string_view reference) {
  if (!IsValidDeveloperSecretReference(reference)) {
    return std::nullopt;
  }
  constexpr std::string_view kPrefix = "ahoi-keychain:";
  return std::string(reference.substr(kPrefix.size()));
}

NSMutableDictionary* BaseQuery(std::string_view account,
                               std::string_view access_group) {
  NSMutableDictionary* query = [NSMutableDictionary dictionaryWithDictionary:@{
    CFToNSPtrCast(kSecClass) : CFToNSPtrCast(kSecClassGenericPassword),
    CFToNSPtrCast(kSecAttrService) : base::SysUTF8ToNSString(kKeychainService),
    CFToNSPtrCast(kSecAttrAccount) : base::SysUTF8ToNSString(account),
    CFToNSPtrCast(kSecAttrSynchronizable) : @NO,
  }];
  if (!access_group.empty()) {
    query[CFToNSPtrCast(kSecAttrAccessGroup)] =
        base::SysUTF8ToNSString(access_group);
  }
  return query;
}

class MacDeveloperSecretStore final : public DeveloperSecretStore {
 public:
  MacDeveloperSecretStore(crypto::apple::KeychainV2* keychain,
                          std::string access_group)
      : keychain_(keychain), access_group_(std::move(access_group)) {
    CHECK(keychain_);
  }
  MacDeveloperSecretStore(const MacDeveloperSecretStore&) = delete;
  MacDeveloperSecretStore& operator=(const MacDeveloperSecretStore&) = delete;
  ~MacDeveloperSecretStore() override = default;

  std::optional<std::string> Store(std::string_view label,
                                   std::string_view secret) override {
    if (!IsValidLabel(label) || secret.empty() ||
        secret.size() > kMaxDeveloperHeaderValueBytes ||
        !IsValidDeveloperHeaderValue(secret)) {
      return std::nullopt;
    }
    for (int attempt = 0; attempt < kReferenceCreationAttempts; ++attempt) {
      const std::string account =
          base::Uuid::GenerateRandomV4().AsLowercaseString();
      NSMutableDictionary* attributes = BaseQuery(account, access_group_);
      [attributes addEntriesFromDictionary:@{
        CFToNSPtrCast(kSecAttrLabel) : base::SysUTF8ToNSString(label),
        CFToNSPtrCast(kSecAttrAccessible) :
            CFToNSPtrCast(kSecAttrAccessibleWhenUnlockedThisDeviceOnly),
        CFToNSPtrCast(kSecValueData) : [NSData dataWithBytes:secret.data()
                                                      length:secret.size()],
      }];
      const OSStatus status =
          keychain_->ItemAdd(NSToCFPtrCast(attributes), /*result=*/nullptr);
      if (status == errSecSuccess) {
        return "ahoi-keychain:" + account;
      }
      if (status != errSecDuplicateItem) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<std::string> Resolve(
      std::string_view reference) const override {
    const std::optional<std::string> account = AccountForReference(reference);
    if (!account) {
      return std::nullopt;
    }
    NSMutableDictionary* query = BaseQuery(*account, access_group_);
    query[CFToNSPtrCast(kSecMatchLimit)] = CFToNSPtrCast(kSecMatchLimitOne);
    query[CFToNSPtrCast(kSecReturnData)] = @YES;
    base::apple::ScopedCFTypeRef<CFTypeRef> result;
    const OSStatus status = keychain_->ItemCopyMatching(
        NSToCFPtrCast(query), result.InitializeInto());
    if (status != errSecSuccess || !result) {
      return std::nullopt;
    }
    CFDataRef data = base::apple::CFCast<CFDataRef>(result.get());
    if (!data) {
      return std::nullopt;
    }
    base::span<const uint8_t> bytes = base::apple::CFDataToSpan(data);
    if (bytes.empty() || bytes.size() > kMaxDeveloperHeaderValueBytes) {
      return std::nullopt;
    }
    std::string secret(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
    if (!IsValidDeveloperHeaderValue(secret)) {
      return std::nullopt;
    }
    return secret;
  }

  bool Remove(std::string_view reference) override {
    const std::optional<std::string> account = AccountForReference(reference);
    if (!account) {
      return false;
    }
    NSMutableDictionary* query = BaseQuery(*account, access_group_);
    return keychain_->ItemDelete(NSToCFPtrCast(query)) == errSecSuccess;
  }

 private:
  const raw_ptr<crypto::apple::KeychainV2> keychain_;
  const std::string access_group_;
};

}  // namespace

std::unique_ptr<DeveloperSecretStore> CreatePlatformDeveloperSecretStore() {
  return std::make_unique<MacDeveloperSecretStore>(
      &crypto::apple::KeychainV2::GetInstance(), std::string());
}

std::unique_ptr<DeveloperSecretStore> CreateMacDeveloperSecretStoreForTesting(
    crypto::apple::KeychainV2* keychain,
    std::string access_group) {
  if (!keychain) {
    return nullptr;
  }
  return std::make_unique<MacDeveloperSecretStore>(keychain,
                                                   std::move(access_group));
}

}  // namespace ahoi
