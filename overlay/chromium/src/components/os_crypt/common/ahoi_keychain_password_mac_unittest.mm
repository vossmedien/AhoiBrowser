// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ahoi: Ahoi's own "Ahoi Safe Storage" item and the one-time adoption of the
// legacy "Chromium Safe Storage" secret. Everything runs against an in-memory
// fake; no test touches the real Keychain.

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/types/expected.h"
#include "build/branding_buildflags.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "crypto/apple/fake_keychain_v2.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(GOOGLE_CHROME_BRANDING)

namespace {

constexpr char kAhoiService[] = "Ahoi Safe Storage";
constexpr char kAhoiAccount[] = "Ahoi";
constexpr char kLegacyService[] = "Chromium Safe Storage";
constexpr char kLegacyAccount[] = "Chromium";
constexpr char kLegacySecret[] = "bGVnYWN5LXNlY3JldC0xMjM0";

// A generic-password store keyed by service and account, with per-item
// lookup errors and an injectable add result. Deleting or updating an item
// fails the test: the legacy item must never be modified.
class ItemKeychain : public crypto::apple::FakeKeychainV2 {
 public:
  using Key = std::pair<std::string, std::string>;

  ItemKeychain() : FakeKeychainV2("test-access-group") {}

  void Put(std::string_view service,
           std::string_view account,
           std::string_view secret) {
    items_[Key(service, account)] =
        base::ToVector(base::as_byte_span(secret));
  }

  bool Has(std::string_view service, std::string_view account) const {
    return items_.contains(Key(service, account));
  }

  std::string Get(std::string_view service, std::string_view account) const {
    const std::vector<uint8_t>& bytes = items_.at(Key(service, account));
    return std::string(bytes.begin(), bytes.end());
  }

  void FailFind(std::string_view service, OSStatus error) {
    find_errors_[std::string(service)] = error;
  }
  void AllowFind(std::string_view service) {
    find_errors_.erase(std::string(service));
  }

  void set_add_result(OSStatus result) { add_result_ = result; }
  int add_count() const { return add_count_; }
  size_t item_count() const { return items_.size(); }

  // crypto::apple::KeychainV2:
  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    auto error = find_errors_.find(std::string(service_name));
    if (error != find_errors_.end()) {
      return base::unexpected(error->second);
    }
    auto item = items_.find(Key(service_name, account_name));
    if (item == items_.end()) {
      return base::unexpected(errSecItemNotFound);
    }
    return item->second;
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    ++add_count_;
    if (add_result_ != noErr) {
      return add_result_;
    }
    Key key(service_name, account_name);
    if (items_.contains(key)) {
      return errSecDuplicateItem;
    }
    items_[key] = base::ToVector(password);
    return noErr;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    ADD_FAILURE() << "Keychain items must not be deleted";
    return errSecWrPerm;
  }

  OSStatus ItemUpdate(CFDictionaryRef query,
                      CFDictionaryRef keychain_data) override {
    ADD_FAILURE() << "Keychain items must not be updated";
    return errSecWrPerm;
  }

 private:
  std::map<Key, std::vector<uint8_t>> items_;
  std::map<std::string, OSStatus> find_errors_;
  OSStatus add_result_ = noErr;
  int add_count_ = 0;
};

std::string GetPassword(ItemKeychain& keychain) {
  return KeychainPassword(keychain).GetPassword();
}

TEST(AhoiKeychainPasswordTest, UsesAhoiServiceAndAccountNames) {
  EXPECT_EQ(kAhoiService, KeychainPassword::GetServiceName());
  EXPECT_EQ(kAhoiAccount, KeychainPassword::GetAccountName());
}

// A fresh Mac: only Ahoi's item is created, with a new random secret.
TEST(AhoiKeychainPasswordTest, FreshKeychainCreatesOnlyAhoiItem) {
  ItemKeychain keychain;
  std::string password = GetPassword(keychain);

  EXPECT_EQ(24u, password.length());
  EXPECT_EQ(1u, keychain.item_count());
  ASSERT_TRUE(keychain.Has(kAhoiService, kAhoiAccount));
  EXPECT_EQ(password, keychain.Get(kAhoiService, kAhoiAccount));
  EXPECT_FALSE(keychain.Has(kLegacyService, kLegacyAccount));
}

// An upgrade: the legacy secret is copied byte for byte, once, and the
// legacy item stays as it was.
TEST(AhoiKeychainPasswordTest, AdoptsLegacySecretOnce) {
  ItemKeychain keychain;
  keychain.Put(kLegacyService, kLegacyAccount, kLegacySecret);

  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(1, keychain.add_count());
  EXPECT_EQ(2u, keychain.item_count());
  EXPECT_EQ(kLegacySecret, keychain.Get(kAhoiService, kAhoiAccount));
  EXPECT_EQ(kLegacySecret, keychain.Get(kLegacyService, kLegacyAccount));

  // Later starts read Ahoi's item only and never copy again, even when the
  // legacy item is no longer readable.
  keychain.FailFind(kLegacyService, errSecAuthFailed);
  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(1, keychain.add_count());
  EXPECT_EQ(2u, keychain.item_count());
}

// Once Ahoi has its own item, a different legacy secret is ignored.
TEST(AhoiKeychainPasswordTest, PrefersAhoiItemOverLegacyItem) {
  ItemKeychain keychain;
  keychain.Put(kAhoiService, kAhoiAccount, "YWhvaS1zZWNyZXQtNTY3ODkw");
  keychain.Put(kLegacyService, kLegacyAccount, kLegacySecret);

  EXPECT_EQ("YWhvaS1zZWNyZXQtNTY3ODkw", GetPassword(keychain));
  EXPECT_EQ(0, keychain.add_count());
  EXPECT_EQ(kLegacySecret, keychain.Get(kLegacyService, kLegacyAccount));
}

// A failed copy keeps the legacy secret for this run, creates no new key,
// and succeeds on the next start.
TEST(AhoiKeychainPasswordTest, CopyFailureFallsBackAndRetries) {
  ItemKeychain keychain;
  keychain.Put(kLegacyService, kLegacyAccount, kLegacySecret);
  keychain.set_add_result(errSecWrPerm);

  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(1, keychain.add_count());
  EXPECT_FALSE(keychain.Has(kAhoiService, kAhoiAccount));

  keychain.set_add_result(noErr);
  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(2, keychain.add_count());
  EXPECT_EQ(kLegacySecret, keychain.Get(kAhoiService, kAhoiAccount));
  EXPECT_EQ(kLegacySecret, keychain.Get(kLegacyService, kLegacyAccount));
}

// A legacy item that cannot be read (for example a denied Keychain prompt)
// must not be replaced by a new random key that would orphan existing data.
TEST(AhoiKeychainPasswordTest, LegacyLookupErrorCreatesNoKey) {
  ItemKeychain keychain;
  keychain.Put(kLegacyService, kLegacyAccount, kLegacySecret);
  keychain.FailFind(kLegacyService, errSecAuthFailed);

  EXPECT_TRUE(GetPassword(keychain).empty());
  EXPECT_EQ(0, keychain.add_count());
  EXPECT_FALSE(keychain.Has(kAhoiService, kAhoiAccount));

  // The next start, with access granted, adopts the legacy secret.
  keychain.AllowFind(kLegacyService);
  EXPECT_EQ(kLegacySecret, GetPassword(keychain));
  EXPECT_EQ(kLegacySecret, keychain.Get(kAhoiService, kAhoiAccount));
}

// An unreadable Ahoi item does not fall through to the legacy item.
TEST(AhoiKeychainPasswordTest, AhoiLookupErrorDoesNotTouchLegacyItem) {
  ItemKeychain keychain;
  keychain.Put(kLegacyService, kLegacyAccount, kLegacySecret);
  keychain.FailFind(kAhoiService, errSecAuthFailed);

  EXPECT_TRUE(GetPassword(keychain).empty());
  EXPECT_EQ(0, keychain.add_count());
  EXPECT_FALSE(keychain.Has(kAhoiService, kAhoiAccount));
}

}  // namespace

#endif  // !BUILDFLAG(GOOGLE_CHROME_BRANDING)
