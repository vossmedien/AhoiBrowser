// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_cookie_chromium_adapter.h"

#include <optional>

#include "net/base/schemeful_site.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(DeveloperCookieChromiumAdapterTest,
     CreatesNewPartitionKeyThroughStorageFactory) {
  const GURL site_url("https://app.example.test/path");
  const std::optional<net::CookiePartitionKey> key =
      ResolveDeveloperCookiePartitionKey(site_url, /*partitioned=*/true,
                                         std::nullopt);

  ASSERT_TRUE(key);
  EXPECT_EQ(net::SchemefulSite(site_url), key->site());
  EXPECT_FALSE(key->IsThirdParty());
  EXPECT_FALSE(key->nonce());
}

TEST(DeveloperCookieChromiumAdapterTest,
     PreservesDistinctExistingKeysAndToggleRemovesPartitioning) {
  const std::optional<net::CookiePartitionKey> first =
      net::CookiePartitionKey::FromStorageKeyComponents(
          net::SchemefulSite(GURL("https://first.example.test")),
          net::CookiePartitionKey::AncestorChainBit::kCrossSite, std::nullopt);
  const std::optional<net::CookiePartitionKey> second =
      net::CookiePartitionKey::FromStorageKeyComponents(
          net::SchemefulSite(GURL("https://second.example.test")),
          net::CookiePartitionKey::AncestorChainBit::kSameSite, std::nullopt);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  EXPECT_EQ(first, ResolveDeveloperCookiePartitionKey(
                       GURL("https://cookie.example.test"),
                       /*partitioned=*/true, first));
  EXPECT_EQ(second, ResolveDeveloperCookiePartitionKey(
                        GURL("https://cookie.example.test"),
                        /*partitioned=*/true, second));
  EXPECT_FALSE(ResolveDeveloperCookiePartitionKey(
      GURL("https://cookie.example.test"), /*partitioned=*/false, first));
}

}  // namespace
}  // namespace ahoi
