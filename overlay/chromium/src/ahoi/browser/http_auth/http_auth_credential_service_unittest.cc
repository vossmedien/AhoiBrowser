// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_credential_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_prefs.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/mock_password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "net/base/auth.h"
#include "net/http/http_auth.h"
#include "services/network/public/mojom/clear_data_filter.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace ahoi {

namespace {

using ::testing::_;
using ::testing::NiceMock;

HttpAuthProtectionSpace MakeSpace(
    std::string url,
    net::HttpAuth::Target target = net::HttpAuth::AUTH_SERVER,
    net::HttpAuth::Scheme scheme = net::HttpAuth::AUTH_SCHEME_BASIC,
    std::string realm = "Ahoi",
    std::vector<std::string> permitted_paths = {},
    net::NetworkAnonymizationKey network_anonymization_key = {}) {
  return HttpAuthProtectionSpace(target, url::SchemeHostPort{GURL(url)}, scheme,
                                 std::move(realm), std::move(permitted_paths),
                                 std::move(network_anonymization_key));
}

password_manager::StoredCredential MakeStoredCredential(
    const HttpAuthProtectionSpace& protection_space,
    std::u16string username,
    std::u16string password,
    base::Time last_used = base::Time::Now()) {
  password_manager::PasswordForm form;
  form.scheme = protection_space.scheme == net::HttpAuth::AUTH_SCHEME_BASIC
                    ? password_manager::PasswordForm::Scheme::kBasic
                    : password_manager::PasswordForm::Scheme::kDigest;
  form.signon_realm = protection_space.SignonRealm();
  form.url = protection_space.OriginUrl();
  form.username_value = std::move(username);
  form.password_value = std::move(password);
  form.date_last_used = last_used;
  return password_manager::FromPasswordForm(std::move(form));
}

class HttpAuthCredentialServiceTest : public ::testing::Test {
 public:
  HttpAuthCredentialServiceTest()
      : store_(base::MakeRefCounted<
               NiceMock<password_manager::MockPasswordStoreInterface>>()) {}

  void SetUp() override {
    HttpAuthCredentialService::RegisterProfilePrefs(prefs_.registry());
    service_ = std::make_unique<HttpAuthCredentialService>(&prefs_, store_);

    ON_CALL(*store_, GetLogins)
        .WillByDefault(
            [this](const password_manager::PasswordFormDigest&,
                   base::WeakPtr<password_manager::PasswordStoreConsumer>
                       consumer) {
              ASSERT_TRUE(consumer);
              consumer->OnGetPasswordStoreResultsOrErrorFrom(
                  store_.get(), std::move(lookup_results_));
            });
    ON_CALL(*store_, AddLogin)
        .WillByDefault([](password_manager::StoredCredential,
                          base::OnceClosure done) { std::move(done).Run(); });
    ON_CALL(*store_, UpdateLogin)
        .WillByDefault([](password_manager::StoredCredential,
                          base::OnceClosure done) { std::move(done).Run(); });
    ON_CALL(*store_, UpdateLoginWithPrimaryKey)
        .WillByDefault([](password_manager::StoredCredential,
                          const password_manager::StoredCredential&,
                          base::OnceClosure done) { std::move(done).Run(); });
  }

 protected:
  void SetLookupResults(password_manager::LoginsResult results) {
    lookup_results_ = std::move(results);
  }

  void Save(const HttpAuthProtectionSpace& protection_space,
            std::string_view path,
            std::u16string username,
            std::u16string password) {
    SetLookupResults({});
    bool completed = false;
    service_->RecordSuccessfulAuthentication(
        protection_space, path,
        net::AuthCredentials(std::move(username), std::move(password)),
        HttpAuthRequestContext::kRegular,
        /*user_confirmed_insecure_http=*/true,
        base::BindLambdaForTesting([&completed]() { completed = true; }));
    EXPECT_TRUE(completed);
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
  scoped_refptr<NiceMock<password_manager::MockPasswordStoreInterface>> store_;
  password_manager::LoginsResult lookup_results_;
  std::unique_ptr<HttpAuthCredentialService> service_;
};

TEST(HttpAuthProtectionSpaceTest, SeparatesEveryProtectionDimension) {
  const HttpAuthProtectionSpace server =
      MakeSpace("https://auth.example.test:8443/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"});
  const net::NetworkAnonymizationKey other_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://top.example.test")));

  EXPECT_TRUE(server.Matches(server, "/private/next.html"));
  EXPECT_FALSE(server.Matches(server, "/private2/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("https://auth.example.test:8443/private/index.html",
                net::HttpAuth::AUTH_PROXY, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}),
      "/private/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("http://auth.example.test:8443/private/index.html"),
      "/private/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("https://auth.example.test:8443/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_DIGEST,
                "Realm", {"/private/index.html"}),
      "/private/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("https://auth.example.test:8443/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Other", {"/private/index.html"}),
      "/private/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("https://auth.example.test:8443/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}, other_nak),
      "/private/next.html"));
  EXPECT_FALSE(server.Matches(
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}),
      "/private/next.html"));
}

TEST(HttpAuthProtectionSpaceTest, ChallengeAcceptsOnlyBasicAndDigest) {
  net::AuthChallengeInfo challenge;
  challenge.challenger =
      url::SchemeHostPort{GURL("https://auth.example.test:8443")};
  challenge.scheme = "BaSiC";
  challenge.realm = "Realm";
  challenge.path = "/private/login";

  std::optional<HttpAuthProtectionSpace> basic =
      HttpAuthProtectionSpace::FromChallenge(
          challenge, GURL("https://auth.example.test:8443/private/login"));
  ASSERT_TRUE(basic);
  EXPECT_EQ(net::HttpAuth::AUTH_SCHEME_BASIC, basic->scheme);
  EXPECT_TRUE(basic->AllowsPath("/private/next"));

  challenge.scheme = "Digest";
  std::optional<HttpAuthProtectionSpace> digest =
      HttpAuthProtectionSpace::FromChallenge(
          challenge, GURL("https://auth.example.test:8443/private/login"));
  ASSERT_TRUE(digest);
  EXPECT_EQ(net::HttpAuth::AUTH_SCHEME_DIGEST, digest->scheme);

  challenge.scheme = "NTLM";
  EXPECT_FALSE(HttpAuthProtectionSpace::FromChallenge(
      challenge, GURL("https://auth.example.test:8443/private/login")));
}

TEST_F(HttpAuthCredentialServiceTest, MetadataPreferenceIsNotSyncable) {
  EXPECT_EQ(0u, prefs_.registry()->GetRegistrationFlags(
                    http_auth_prefs::kCredentialMetadata));
}

TEST_F(HttpAuthCredentialServiceTest,
       ManagementCredentialSnapshotFailsClosedOnMalformedState) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html");
  Save(protection_space, "/private/index.html", u"alice", u"password");

  base::DictValue root =
      prefs_.GetDict(http_auth_prefs::kCredentialMetadata).Clone();
  base::ListValue* credentials = root.FindList("credentials");
  ASSERT_TRUE(credentials);
  credentials->Append(base::DictValue());
  prefs_.SetDict(http_auth_prefs::kCredentialMetadata, std::move(root));

  EXPECT_TRUE(service_->GetMetadataSnapshot().empty());
}

TEST_F(HttpAuthCredentialServiceTest,
       ManagementNeverSaveSnapshotFailsClosedOnMalformedState) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html");
  ASSERT_TRUE(service_->SetNeverSaveForRealm(protection_space, true,
                                             HttpAuthRequestContext::kRegular));

  base::DictValue root =
      prefs_.GetDict(http_auth_prefs::kCredentialMetadata).Clone();
  base::ListValue* never_save = root.FindList("never_save");
  ASSERT_TRUE(never_save);
  never_save->Append(base::DictValue());
  prefs_.SetDict(http_auth_prefs::kCredentialMetadata, std::move(root));

  EXPECT_TRUE(service_->GetNeverSaveSnapshot().empty());
}

TEST_F(HttpAuthCredentialServiceTest, RanksPreferredAccountBeforeLastSuccess) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"});
  Save(protection_space, "/private/index.html", u"alice", u"alice-password");
  Save(protection_space, "/private/index.html", u"bob", u"bob-password");
  ASSERT_TRUE(service_->SetPreferredCredential(
      protection_space, u"bob", HttpAuthRequestContext::kRegular));

  password_manager::LoginsResult lookup_results;
  lookup_results.push_back(
      MakeStoredCredential(protection_space, u"alice", u"alice-password"));
  lookup_results.push_back(
      MakeStoredCredential(protection_space, u"bob", u"bob-password"));
  SetLookupResults(std::move(lookup_results));
  std::vector<HttpAuthCredential> credentials;
  service_->GetCredentials(
      protection_space, "/private/next.html", HttpAuthRequestContext::kRegular,
      HttpAuthSelectionMode::kAutomatic,
      base::BindLambdaForTesting(
          [&credentials](std::vector<HttpAuthCredential> result) {
            credentials = std::move(result);
          }));

  ASSERT_EQ(2u, credentials.size());
  EXPECT_EQ(u"bob", credentials[0].metadata.username);
  EXPECT_EQ(u"alice", credentials[1].metadata.username);
  EXPECT_EQ(u"bob-password", credentials[0].password);
}

TEST_F(HttpAuthCredentialServiceTest, SingleFailureRetainsSavedMetadata) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"});
  Save(protection_space, "/private/index.html", u"alice", u"password");
  const std::vector<HttpAuthCredentialMetadata> before =
      service_->GetMetadataSnapshot();
  ASSERT_EQ(1u, before.size());

  service_->RecordAuthenticationFailure(protection_space, "/private/index.html",
                                        u"alice",
                                        HttpAuthRequestContext::kRegular);
  EXPECT_EQ(before, service_->GetMetadataSnapshot());
}

TEST_F(HttpAuthCredentialServiceTest,
       DeleteRemovesPartitionedMetadataForSharedPasswordStoreSecret) {
  const net::NetworkAnonymizationKey first_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://first-top.test")));
  const net::NetworkAnonymizationKey second_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://second-top.test")));
  const HttpAuthProtectionSpace first =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}, first_nak);
  const HttpAuthProtectionSpace second =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}, second_nak);
  Save(first, "/private/index.html", u"alice", u"password");
  Save(second, "/private/index.html", u"alice", u"password");
  ASSERT_EQ(service_->GetMetadataSnapshot().size(), 2u);

  SetLookupResults({});
  bool completed = false;
  service_->DeleteCredential(
      first, u"alice", HttpAuthRequestContext::kRegular,
      base::BindLambdaForTesting([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);
  EXPECT_TRUE(service_->GetMetadataSnapshot().empty());
}

TEST_F(HttpAuthCredentialServiceTest,
       UpdateRenamesPasswordStorePrimaryKeyAndEveryPartitionedMetadataRow) {
  const net::NetworkAnonymizationKey first_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://first-top.test")));
  const net::NetworkAnonymizationKey second_nak =
      net::NetworkAnonymizationKey::CreateSameSite(
          net::SchemefulSite(GURL("https://second-top.test")));
  const HttpAuthProtectionSpace first =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}, first_nak);
  const HttpAuthProtectionSpace second =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"}, second_nak);
  Save(first, "/private/index.html", u"alice", u"old-password");
  Save(second, "/private/index.html", u"alice", u"old-password");

  password_manager::LoginsResult lookup_results;
  lookup_results.push_back(
      MakeStoredCredential(first, u"alice", u"old-password"));
  SetLookupResults(std::move(lookup_results));

  std::u16string stored_old_username;
  std::u16string stored_new_username;
  std::u16string stored_new_password;
  EXPECT_CALL(*store_, UpdateLoginWithPrimaryKey(_, _, _))
      .WillOnce([&](password_manager::StoredCredential updated,
                    const password_manager::StoredCredential& old_key,
                    base::OnceClosure done) {
        stored_old_username = old_key.username_value;
        stored_new_username = updated.username_value;
        stored_new_password = updated.password_value;
        std::move(done).Run();
      });

  bool update_succeeded = false;
  service_->UpdateCredential(
      first, u"alice", u"renamed-alice", u"new-password",
      HttpAuthRequestContext::kRegular,
      base::BindLambdaForTesting(
          [&update_succeeded](bool success) { update_succeeded = success; }));

  EXPECT_TRUE(update_succeeded);
  EXPECT_EQ(u"alice", stored_old_username);
  EXPECT_EQ(u"renamed-alice", stored_new_username);
  EXPECT_EQ(u"new-password", stored_new_password);
  const std::vector<HttpAuthCredentialMetadata> metadata =
      service_->GetMetadataSnapshot();
  ASSERT_EQ(2u, metadata.size());
  EXPECT_TRUE(std::ranges::all_of(metadata, [](const auto& item) {
    return item.username == u"renamed-alice";
  }));
}

TEST_F(HttpAuthCredentialServiceTest,
       UpdateFailsClosedOnUsernameCollisionWithoutPasswordStoreWrite) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html");
  Save(protection_space, "/private/index.html", u"alice", u"alice-password");
  Save(protection_space, "/private/index.html", u"bob", u"bob-password");

  EXPECT_CALL(*store_, GetLogins).Times(0);
  EXPECT_CALL(*store_, UpdateLogin).Times(0);
  EXPECT_CALL(*store_, UpdateLoginWithPrimaryKey).Times(0);
  bool update_succeeded = true;
  service_->UpdateCredential(
      protection_space, u"alice", u"bob", u"new-password",
      HttpAuthRequestContext::kRegular,
      base::BindLambdaForTesting(
          [&update_succeeded](bool success) { update_succeeded = success; }));

  EXPECT_FALSE(update_succeeded);
  const std::vector<HttpAuthCredentialMetadata> metadata =
      service_->GetMetadataSnapshot();
  ASSERT_EQ(2u, metadata.size());
  EXPECT_TRUE(std::ranges::any_of(
      metadata, [](const auto& item) { return item.username == u"alice"; }));
  EXPECT_TRUE(std::ranges::any_of(
      metadata, [](const auto& item) { return item.username == u"bob"; }));
}

TEST_F(HttpAuthCredentialServiceTest,
       PasswordOnlyUpdateKeepsPrimaryKeyAndUsesPasswordStoreUpdate) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html");
  Save(protection_space, "/private/index.html", u"alice", u"old-password");
  password_manager::LoginsResult lookup_results;
  lookup_results.push_back(
      MakeStoredCredential(protection_space, u"alice", u"old-password"));
  SetLookupResults(std::move(lookup_results));

  EXPECT_CALL(*store_, UpdateLoginWithPrimaryKey).Times(0);
  std::u16string stored_password;
  EXPECT_CALL(*store_, UpdateLogin(_, _))
      .WillOnce([&stored_password](password_manager::StoredCredential updated,
                                   base::OnceClosure done) {
        stored_password = updated.password_value;
        std::move(done).Run();
      });
  bool update_succeeded = false;
  service_->UpdateCredential(
      protection_space, u"alice", u"alice", u"new-password",
      HttpAuthRequestContext::kRegular,
      base::BindLambdaForTesting(
          [&update_succeeded](bool success) { update_succeeded = success; }));

  EXPECT_TRUE(update_succeeded);
  EXPECT_EQ(u"new-password", stored_password);
  const std::vector<HttpAuthCredentialMetadata> metadata =
      service_->GetMetadataSnapshot();
  ASSERT_EQ(1u, metadata.size());
  EXPECT_EQ(u"alice", metadata.front().username);
}

TEST_F(HttpAuthCredentialServiceTest, IncognitoDoesNotReadOrWrite) {
  HttpAuthCredentialService incognito_service(&prefs_, store_, true);
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html");
  EXPECT_CALL(*store_, GetLogins).Times(0);
  EXPECT_CALL(*store_, AddLogin).Times(0);
  EXPECT_CALL(*store_, UpdateLogin).Times(0);
  EXPECT_CALL(*store_, UpdateLoginWithPrimaryKey).Times(0);

  bool completed = false;
  incognito_service.RecordSuccessfulAuthentication(
      protection_space, "/private/index.html",
      net::AuthCredentials(u"alice", u"password"),
      HttpAuthRequestContext::kIncognito,
      /*user_confirmed_insecure_http=*/true,
      base::BindLambdaForTesting([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);
  EXPECT_FALSE(service_->SetNeverSaveForRealm(
      protection_space, true, HttpAuthRequestContext::kIncognito));
  EXPECT_FALSE(incognito_service.SetNeverSaveForRealm(
      protection_space, true, HttpAuthRequestContext::kRegular));
  EXPECT_FALSE(service_->IsNeverSaveForRealm(protection_space));
  EXPECT_FALSE(service_->SetPreferredCredential(
      protection_space, u"alice", HttpAuthRequestContext::kIncognito));

  bool update_succeeded = true;
  incognito_service.UpdateCredential(
      protection_space, u"alice", u"renamed", u"new-password",
      HttpAuthRequestContext::kIncognito,
      base::BindLambdaForTesting(
          [&update_succeeded](bool success) { update_succeeded = success; }));
  EXPECT_FALSE(update_succeeded);
}

TEST_F(HttpAuthCredentialServiceTest,
       IncognitoReadsOnlyAfterExplicitUserSelection) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"});
  Save(protection_space, "/private/index.html", u"alice", u"password");
  password_manager::LoginsResult lookup_results;
  lookup_results.push_back(
      MakeStoredCredential(protection_space, u"alice", u"password"));

  EXPECT_CALL(*store_, GetLogins).Times(0);
  std::vector<HttpAuthCredential> automatic;
  service_->GetCredentials(
      protection_space, "/private/next", HttpAuthRequestContext::kIncognito,
      HttpAuthSelectionMode::kAutomatic,
      base::BindLambdaForTesting(
          [&automatic](std::vector<HttpAuthCredential> result) {
            automatic = std::move(result);
          }));
  EXPECT_TRUE(automatic.empty());
  testing::Mock::VerifyAndClearExpectations(store_.get());

  SetLookupResults(std::move(lookup_results));
  std::vector<HttpAuthCredential> explicit_selection;
  service_->GetCredentials(
      protection_space, "/private/next", HttpAuthRequestContext::kIncognito,
      HttpAuthSelectionMode::kExplicitUserSelection,
      base::BindLambdaForTesting(
          [&explicit_selection](std::vector<HttpAuthCredential> result) {
            explicit_selection = std::move(result);
          }));
  ASSERT_EQ(1u, explicit_selection.size());
  EXPECT_EQ(u"alice", explicit_selection.front().metadata.username);
}

TEST_F(HttpAuthCredentialServiceTest,
       NeverSaveSuppressesWritesButDoesNotHideExistingAccounts) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("https://auth.example.test/private/index.html",
                net::HttpAuth::AUTH_SERVER, net::HttpAuth::AUTH_SCHEME_BASIC,
                "Realm", {"/private/index.html"});
  Save(protection_space, "/private/index.html", u"alice", u"old-password");
  ASSERT_TRUE(service_->SetNeverSaveForRealm(protection_space, true,
                                             HttpAuthRequestContext::kRegular));
  ASSERT_EQ(service_->GetNeverSaveSnapshot().size(), 1u);
  EXPECT_TRUE(
      service_->GetNeverSaveSnapshot().front().MatchesRealm(protection_space));

  password_manager::LoginsResult lookup_results;
  lookup_results.push_back(
      MakeStoredCredential(protection_space, u"alice", u"old-password"));
  SetLookupResults(std::move(lookup_results));
  std::vector<HttpAuthCredential> credentials;
  service_->GetCredentials(
      protection_space, "/private/next", HttpAuthRequestContext::kRegular,
      HttpAuthSelectionMode::kAutomatic,
      base::BindLambdaForTesting(
          [&credentials](std::vector<HttpAuthCredential> result) {
            credentials = std::move(result);
          }));
  ASSERT_EQ(1u, credentials.size());
  EXPECT_EQ(u"old-password", credentials.front().password);

  EXPECT_CALL(*store_, AddLogin).Times(0);
  EXPECT_CALL(*store_, UpdateLogin).Times(0);
  bool completed = false;
  service_->RecordSuccessfulAuthentication(
      protection_space, "/private/index.html",
      net::AuthCredentials(u"alice", u"new-password"),
      HttpAuthRequestContext::kRegular,
      /*user_confirmed_insecure_http=*/true,
      base::BindLambdaForTesting([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);
}

TEST_F(HttpAuthCredentialServiceTest,
       InsecureHttpRequiresExplicitSaveConfirmation) {
  const HttpAuthProtectionSpace protection_space =
      MakeSpace("http://auth.example.test/private/index.html");
  EXPECT_CALL(*store_, GetLogins).Times(0);
  EXPECT_CALL(*store_, AddLogin).Times(0);
  EXPECT_CALL(*store_, UpdateLogin).Times(0);

  bool completed = false;
  service_->RecordSuccessfulAuthentication(
      protection_space, "/private/index.html",
      net::AuthCredentials(u"alice", u"password"),
      HttpAuthRequestContext::kRegular,
      /*user_confirmed_insecure_http=*/false,
      base::BindLambdaForTesting([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);
}

TEST_F(HttpAuthCredentialServiceTest, CacheFilterIsExactOriginOnly) {
  const HttpAuthProtectionSpace server =
      MakeSpace("https://auth.example.test:8443/private/index.html");
  network::mojom::ClearDataFilterPtr filter =
      HttpAuthCredentialService::BuildHttpAuthCacheFilter(server);
  ASSERT_TRUE(filter);
  EXPECT_EQ(network::mojom::ClearDataFilter::Type::DELETE_MATCHES,
            filter->type);
  ASSERT_EQ(1u, filter->origins.size());
  EXPECT_TRUE(filter->domains.empty());
  EXPECT_EQ("https", filter->origins[0].scheme());
  EXPECT_EQ("auth.example.test", filter->origins[0].host());
  EXPECT_EQ(8443, filter->origins[0].port());

  const HttpAuthProtectionSpace proxy =
      MakeSpace("http://proxy.example.test:3128", net::HttpAuth::AUTH_PROXY);
  network::mojom::ClearDataFilterPtr proxy_filter =
      HttpAuthCredentialService::BuildHttpAuthCacheFilter(proxy);
  ASSERT_TRUE(proxy_filter);
  ASSERT_EQ(1u, proxy_filter->origins.size());
  EXPECT_EQ("proxy.example.test", proxy_filter->origins[0].host());
  EXPECT_EQ(3128, proxy_filter->origins[0].port());

  HttpAuthProtectionSpace invalid;
  EXPECT_FALSE(HttpAuthCredentialService::BuildHttpAuthCacheFilter(invalid));

  network::mojom::ClearDataFilterPtr current_page_filter =
      HttpAuthCredentialService::BuildHttpAuthCacheFilterForOrigin(
          GURL("https://auth.example.test:9443/path?query"));
  ASSERT_TRUE(current_page_filter);
  ASSERT_EQ(1u, current_page_filter->origins.size());
  EXPECT_EQ("auth.example.test", current_page_filter->origins[0].host());
  EXPECT_EQ(9443, current_page_filter->origins[0].port());
  EXPECT_FALSE(HttpAuthCredentialService::BuildHttpAuthCacheFilterForOrigin(
      GURL("chrome://settings")));
}

}  // namespace

}  // namespace ahoi
