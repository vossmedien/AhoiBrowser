// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_management_model.h"

#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "net/base/network_anonymization_key.h"
#include "net/http/http_auth.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace ahoi {
namespace {

template <typename T>
concept HasPasswordMember = requires(T value) { value.password; };

static_assert(!HasPasswordMember<HttpAuthManagementEntry>);
static_assert(!HasPasswordMember<HttpAuthCredentialMetadata>);

HttpAuthCredentialMetadata Metadata(
    std::string origin,
    std::string realm,
    std::u16string username,
    bool preferred = false,
    net::HttpAuth::Scheme scheme = net::HttpAuth::AUTH_SCHEME_BASIC,
    net::HttpAuth::Target target = net::HttpAuth::AUTH_SERVER,
    net::NetworkAnonymizationKey network_anonymization_key = {}) {
  HttpAuthCredentialMetadata result;
  result.protection_space = HttpAuthProtectionSpace(
      target, url::SchemeHostPort(GURL(origin)), scheme, std::move(realm),
      {"/private/"}, std::move(network_anonymization_key));
  result.username = std::move(username);
  result.preferred = preferred;
  return result;
}

TEST(HttpAuthManagementModelTest, FiltersMetadataWithoutSecretFields) {
  std::vector<HttpAuthCredentialMetadata> metadata;
  metadata.push_back(
      Metadata("https://example.test:8443", "Administration", u"Jörg"));
  metadata.push_back(Metadata("https://other.test", "Staging", u"deploy-user"));

  const auto by_host = BuildHttpAuthManagementEntries(
      metadata, u"example.test", GURL("https://example.test:8443/private/"),
      std::nullopt);
  ASSERT_EQ(by_host.size(), 1u);
  EXPECT_EQ(by_host.front().metadata.username, u"Jörg");

  const auto by_realm =
      BuildHttpAuthManagementEntries(metadata, u"admin", GURL(), std::nullopt);
  ASSERT_EQ(by_realm.size(), 1u);
  EXPECT_EQ(by_realm.front().metadata.protection_space.realm, "Administration");

  const auto by_accentless_username =
      BuildHttpAuthManagementEntries(metadata, u"jorg", GURL(), std::nullopt);
  EXPECT_EQ(by_accentless_username.size(), 1u);

  const auto by_explicit_default_port = BuildHttpAuthManagementEntries(
      metadata, u"other.test:443", GURL(), std::nullopt);
  ASSERT_EQ(by_explicit_default_port.size(), 1u);
  EXPECT_EQ(by_explicit_default_port.front().metadata.username, u"deploy-user");
}

TEST(HttpAuthManagementModelTest, GroupsAndSortsPreferredAccountFirst) {
  HttpAuthCredentialMetadata older =
      Metadata("https://example.test", "Area", u"z-user");
  older.last_successful = base::Time::FromSecondsSinceUnixEpoch(10);
  HttpAuthCredentialMetadata preferred =
      Metadata("https://example.test", "Area", u"a-user", true);
  preferred.last_successful = base::Time::FromSecondsSinceUnixEpoch(1);
  HttpAuthCredentialMetadata other_origin =
      Metadata("https://alpha.test", "Area", u"x-user");

  const auto entries = BuildHttpAuthManagementEntries(
      {older, preferred, other_origin}, u"", GURL(), std::nullopt);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].metadata.protection_space.origin.host(), "alpha.test");
  EXPECT_EQ(entries[1].metadata.username, u"a-user");
  EXPECT_TRUE(entries[1].metadata.preferred);
  EXPECT_EQ(entries[2].metadata.username, u"z-user");
}

TEST(HttpAuthManagementModelTest,
     DeduplicatesPasswordStoreAccountAcrossNetworkPartitions) {
  const net::NetworkAnonymizationKey first_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://first-top.test")));
  const net::NetworkAnonymizationKey active_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://active-top.test")));
  HttpAuthCredentialMetadata first = Metadata(
      "https://example.test", "Realm", u"account", true,
      net::HttpAuth::AUTH_SCHEME_BASIC, net::HttpAuth::AUTH_SERVER, first_nak);
  first.last_successful = base::Time::FromSecondsSinceUnixEpoch(20);
  HttpAuthCredentialMetadata active = Metadata(
      "https://example.test", "Realm", u"account", false,
      net::HttpAuth::AUTH_SCHEME_BASIC, net::HttpAuth::AUTH_SERVER, active_nak);
  active.last_successful = base::Time::FromSecondsSinceUnixEpoch(10);

  const auto entries = BuildHttpAuthManagementEntries(
      {first, active}, u"", GURL("https://example.test/private/"),
      active.protection_space);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_TRUE(entries.front().can_switch_account);
  EXPECT_FALSE(entries.front().metadata.preferred);
  EXPECT_EQ(entries.front().metadata.last_successful, first.last_successful);
  EXPECT_TRUE(
      entries.front().metadata.protection_space.network_anonymization_key ==
      active_nak);
}

TEST(HttpAuthManagementModelTest, SwitchRequiresExactActiveProtectionSpace) {
  const HttpAuthCredentialMetadata selected =
      Metadata("https://example.test:8443", "Admin", u"one");
  const HttpAuthProtectionSpace exact = selected.protection_space;
  EXPECT_TRUE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test:8443/private/page"), exact));

  HttpAuthProtectionSpace other_realm = exact;
  other_realm.realm = "Other";
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test:8443/private/page"), other_realm));
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test/private/page"), exact));
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("http://example.test:8443/private/page"), exact));

  HttpAuthProtectionSpace other_scheme = exact;
  other_scheme.scheme = net::HttpAuth::AUTH_SCHEME_DIGEST;
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test:8443/private/page"), other_scheme));

  HttpAuthProtectionSpace other_target = exact;
  other_target.target = net::HttpAuth::AUTH_PROXY;
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test:8443/private/page"), other_target));

  HttpAuthProtectionSpace other_network = exact;
  other_network.network_anonymization_key =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://top.example.test")));
  EXPECT_FALSE(CanSwitchManagedHttpAuthAccount(
      selected, GURL("https://example.test:8443/private/page"), other_network));
}

TEST(HttpAuthManagementModelTest,
     DestructiveIdentityMatchesPasswordStoreScopeAcrossNetworks) {
  const HttpAuthCredentialMetadata first =
      Metadata("https://example.test", "Realm", u"one");
  HttpAuthCredentialMetadata same = first;
  EXPECT_TRUE(IsSameManagedHttpAuthCredential(first, same));

  same.username = u"two";
  EXPECT_FALSE(IsSameManagedHttpAuthCredential(first, same));
  same = first;
  same.protection_space.realm = "Other";
  EXPECT_FALSE(IsSameManagedHttpAuthRealm(first.protection_space,
                                          same.protection_space));

  same = first;
  same.protection_space.network_anonymization_key =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://top.example.test")));
  EXPECT_TRUE(IsSameManagedHttpAuthRealm(first.protection_space,
                                         same.protection_space));
  EXPECT_TRUE(IsSameManagedHttpAuthCredential(first, same));
}

}  // namespace
}  // namespace ahoi
