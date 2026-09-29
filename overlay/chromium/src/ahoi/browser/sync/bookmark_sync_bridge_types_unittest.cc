// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_bridge_types.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

constexpr char kNativeUuid[] = "93000000-0000-4000-8000-00000000000a";

void ExpectKeyBetween(const std::string& lower,
                      const std::optional<std::string>& upper) {
  SCOPED_TRACE(lower + " < key < " + upper.value_or("unbounded"));
  const auto key = BookmarkSortKeyBetween(lower, upper);
  ASSERT_TRUE(key);
  EXPECT_FALSE(key->empty());
  EXPECT_LE(key->size(), 1024u);
  EXPECT_TRUE(std::ranges::all_of(
      *key, [](unsigned char value) { return value >= '!' && value <= '~'; }));
  EXPECT_GT(*key, lower);
  if (upper) {
    EXPECT_LT(*key, *upper);
  }
}

TEST(BookmarkSyncBridgeTypesTest, NativeKeyRoundTripsBothStorageDomains) {
  const auto expected = base::Uuid::ParseLowercase(kNativeUuid);
  ASSERT_TRUE(expected.is_valid());
  for (const bool account : {false, true}) {
    const std::string key = NativeBookmarkKey(expected, account);
    EXPECT_EQ(std::string(account ? "account:" : "local:") + kNativeUuid, key);
    base::Uuid actual;
    bool actual_account = !account;
    ASSERT_TRUE(ParseNativeBookmarkKey(key, &actual, &actual_account));
    EXPECT_EQ(expected, actual);
    EXPECT_EQ(account, actual_account);
  }
}

TEST(BookmarkSyncBridgeTypesTest, RejectsMalformedNativeKeysAndMissingOutputs) {
  for (const std::string key :
       {"", "local:", "account:", "local:not-a-uuid",
        "local:93000000-0000-4000-8000-00000000000A",
        "Local:93000000-0000-4000-8000-00000000000a",
        "managed:93000000-0000-4000-8000-00000000000a",
        "93000000-0000-4000-8000-00000000000a",
        "local:93000000-0000-4000-8000-00000000000a ",
        "account:93000000-0000-4000-8000-00000000000a:extra"}) {
    SCOPED_TRACE(key);
    base::Uuid uuid;
    bool account = false;
    EXPECT_FALSE(ParseNativeBookmarkKey(key, &uuid, &account));
    EXPECT_FALSE(InitialBookmarkSyncId(key).is_valid());
  }
  const auto key =
      NativeBookmarkKey(base::Uuid::ParseLowercase(kNativeUuid), false);
  base::Uuid uuid;
  bool account = false;
  EXPECT_FALSE(ParseNativeBookmarkKey(key, nullptr, &account));
  EXPECT_FALSE(ParseNativeBookmarkKey(key, &uuid, nullptr));
}

TEST(BookmarkSyncBridgeTypesTest, InitialIdentityIsStableAndStorageScoped) {
  const auto native = base::Uuid::ParseLowercase(kNativeUuid);
  const std::string local = NativeBookmarkKey(native, false);
  const std::string account = NativeBookmarkKey(native, true);
  const auto local_id = InitialBookmarkSyncId(local);
  const auto account_id = InitialBookmarkSyncId(account);
  ASSERT_TRUE(local_id.is_valid());
  ASSERT_TRUE(account_id.is_valid());
  EXPECT_EQ(local_id, InitialBookmarkSyncId(local));
  EXPECT_EQ(account_id, InitialBookmarkSyncId(account));
  EXPECT_NE(local_id, account_id);
  EXPECT_NE(local_id, native);
  EXPECT_NE(account_id, native);
  EXPECT_NE(local_id, InitialBookmarkSyncId(
                          "local:93000000-0000-4000-8000-00000000000b"));
}

TEST(BookmarkSyncBridgeTypesTest, SortKeySupportsEmptyAndUnboundedIntervals) {
  ExpectKeyBetween("", std::nullopt);
  ExpectKeyBetween("", "a");
  ExpectKeyBetween("a", std::nullopt);
  ExpectKeyBetween("~", std::nullopt);
}

TEST(BookmarkSyncBridgeTypesTest, SortKeyFitsAdjacentCharactersAndPrefixes) {
  for (const auto& [lower, upper] :
       std::vector<std::pair<std::string, std::string>>{{"a", "b"},
                                                        {"a~", "b"},
                                                        {"!~", "\""},
                                                        {"a", "aa"},
                                                        {"a!", "a\""},
                                                        {"aa~", "ab"}}) {
    ExpectKeyBetween(lower, upper);
  }
}

TEST(BookmarkSyncBridgeTypesTest, SortKeyUsesSpaceBeforeMinimalUpperPrefix) {
  // A strict prefix can itself be a valid key. None of these intervals is
  // exhausted just because the next upper-bound byte is the minimum '!'.
  for (const auto& [lower, upper] :
       std::vector<std::pair<std::string, std::string>>{{"", "!a"},
                                                        {"", "!!"},
                                                        {"a", "a!a"},
                                                        {"a", "a!!"},
                                                        {"!!!", "!!!!~"}}) {
    ExpectKeyBetween(lower, upper);
  }
}

TEST(BookmarkSyncBridgeTypesTest, SortKeyReportsActuallyExhaustedIntervals) {
  for (const auto& [lower, upper] :
       std::vector<std::pair<std::string, std::string>>{
           {"", "!"}, {"a", "a!"}, {"!", "!!"}, {"~", "~!"}}) {
    SCOPED_TRACE(lower + " < key < " + upper);
    EXPECT_FALSE(BookmarkSortKeyBetween(lower, upper));
  }
  EXPECT_FALSE(BookmarkSortKeyBetween("", ""));
  EXPECT_FALSE(BookmarkSortKeyBetween("a", "a"));
  EXPECT_FALSE(BookmarkSortKeyBetween("b", "a"));
}

TEST(BookmarkSyncBridgeTypesTest, SortKeyHonorsMaximumLengthWithoutOverflow) {
  ExpectKeyBetween(std::string(1023, '~'), std::nullopt);
  ExpectKeyBetween(std::string(1023, '~'), std::string(1024, '~'));
  ExpectKeyBetween(std::string(1024, 'a'), std::string(1023, 'a') + 'c');
  EXPECT_FALSE(BookmarkSortKeyBetween(std::string(1024, '~'), std::nullopt));
  EXPECT_FALSE(BookmarkSortKeyBetween(std::string(1023, '~') + '}',
                                      std::string(1024, '~')));
}

TEST(BookmarkSyncBridgeTypesTest, SortKeyRejectsNonVisibleOrOversizedBounds) {
  for (const std::string& invalid : std::vector<std::string>{
           " ", "a b", "a\n", std::string("a\0", 2), std::string(1, '\x7f'),
           std::string(1, '\x80'), std::string(1025, 'a')}) {
    EXPECT_FALSE(BookmarkSortKeyBetween(invalid, std::nullopt));
    EXPECT_FALSE(BookmarkSortKeyBetween("", invalid));
  }
}

}  // namespace
}  // namespace ahoi::sync
