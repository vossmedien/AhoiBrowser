// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_profile_codec.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/json/json_writer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

url::Origin ExampleOrigin() {
  return url::Origin::Create(GURL("https://example.test/path"));
}

DeveloperProfile MakeProfile() {
  DeveloperProfile profile;
  profile.name = "Example development";
  profile.assets.push_back({
      .id = "example-style",
      .name = "Example style",
      .kind = DeveloperAssetKind::kStyle,
      .enabled = true,
      .source = "body { outline: 1px solid red; }",
      .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                .value = ExampleOrigin().Serialize()},
      .sync_enabled = true,
  });
  profile.assets.push_back({
      .id = "example-script",
      .name = "Example script",
      .kind = DeveloperAssetKind::kJavaScript,
      .enabled = true,
      .source = "document.documentElement.dataset.ahoi = '1';",
      .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                .value = ExampleOrigin().Serialize()},
  });
  profile.user_agent_enabled = true;
  profile.user_agent = "AhoiDev/1.0";
  profile.header_rules_enabled = true;
  profile.header_rules.push_back({.name = "X-Ahoi-Mode",
                                  .value = "dev",
                                  .action = DeveloperHeaderAction::kSet});
  profile.header_rules.push_back({.name = "X-Remove-Me",
                                  .value = "",
                                  .action = DeveloperHeaderAction::kRemove});
  profile.response_header_rules_enabled = true;
  profile.response_header_rules.push_back(
      {.name = "X-Ahoi-Response",
       .value = "enabled",
       .action = DeveloperHeaderAction::kSet});
  profile.response_header_rules.push_back(
      {.name = "X-Remove-Response",
       .value = "",
       .action = DeveloperHeaderAction::kRemove});
  return profile;
}

TEST(DeveloperAssetValidationTest, EnforcesScopeLifetimeAndMainWorldWarning) {
  DeveloperAsset asset = MakeProfile().assets[1];
  asset.javascript_world = DeveloperJavaScriptWorld::kMain;
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kInvalidWorld);
  asset.main_world_warning_accepted = true;
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kNone);

  asset.scope = {.kind = DeveloperAssetScopeKind::kCurrentTab,
                 .value = "tab-session-1"};
  asset.lifetime = DeveloperAssetLifetime::kReload;
  asset.sync_enabled = false;
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kNone);
  EXPECT_FALSE(IsDeveloperAssetPersistable(asset));
  EXPECT_EQ(ValidateDeveloperProfileForPersistence(
                ExampleOrigin(),
                DeveloperProfile{.name = "Ephemeral", .assets = {asset}}),
            DeveloperProfileValidationError::kEphemeralAssetCannotPersist);
}

TEST(DeveloperAssetValidationTest,
     DomainScopeRequiresADeviceLocalAcknowledgementWhenEnabled) {
  DeveloperAsset asset = MakeProfile().assets[0];
  asset.scope = {.kind = DeveloperAssetScopeKind::kDomain,
                 .value = "example.test"};
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kDomainScopeNotAcknowledged);

  asset.domain_scope_warning_accepted = true;
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kNone);

  asset.scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                 .value = ExampleOrigin().Serialize()};
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kInvalidScope);

  asset.domain_scope_warning_accepted = false;
  asset.enabled = false;
  asset.scope = {.kind = DeveloperAssetScopeKind::kDomain,
                 .value = "example.test"};
  EXPECT_EQ(ValidateDeveloperAsset(ExampleOrigin(), asset),
            DeveloperAssetValidationError::kNone);
}

TEST(DeveloperAssetValidationTest, MatchesOriginDomainPathAndTabExplicitly) {
  DeveloperAsset asset = MakeProfile().assets[0];
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://example.test/next")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("https://sub.example.test/next")));

  asset.scope = {.kind = DeveloperAssetScopeKind::kDomain,
                 .value = "example.test"};
  asset.domain_scope_warning_accepted = true;
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://sub.example.test/next")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("http://sub.example.test/next")));

  asset.scope = {.kind = DeveloperAssetScopeKind::kPath, .value = "/docs/"};
  asset.domain_scope_warning_accepted = false;
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://example.test/docs/page")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("https://example.test/api/page")));
}

TEST(DeveloperProfileCodecTest,
     LegacyDomainAssetWithoutConsentMigratesDormantWithoutSourceLoss) {
  DeveloperProfile profile = MakeProfile();
  profile.assets[0].scope = {.kind = DeveloperAssetScopeKind::kDomain,
                             .value = "example.test"};
  profile.assets[0].domain_scope_warning_accepted = true;
  std::optional<base::DictValue> encoded = SerializeDeveloperProfile(profile);
  ASSERT_TRUE(encoded);
  base::ListValue* assets = encoded->FindList("assets");
  ASSERT_TRUE(assets);
  ASSERT_FALSE(assets->empty());
  base::DictValue* first = (*assets)[0].GetIfDict();
  ASSERT_TRUE(first);
  EXPECT_TRUE(first->Remove("domain_scope_warning_accepted"));

  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfile(*encoded);
  ASSERT_TRUE(decoded);
  ASSERT_FALSE(decoded->assets.empty());
  EXPECT_FALSE(decoded->assets[0].enabled);
  EXPECT_FALSE(decoded->assets[0].domain_scope_warning_accepted);
  EXPECT_EQ(decoded->assets[0].source, profile.assets[0].source);
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), *decoded),
            DeveloperProfileValidationError::kNone);
}

TEST(DeveloperProfileCodecTest, RejectsMalformedDomainScopeConsent) {
  std::optional<base::DictValue> encoded =
      SerializeDeveloperProfile(MakeProfile());
  ASSERT_TRUE(encoded);
  base::ListValue* assets = encoded->FindList("assets");
  ASSERT_TRUE(assets);
  ASSERT_FALSE(assets->empty());
  base::DictValue* first = (*assets)[0].GetIfDict();
  ASSERT_TRUE(first);
  first->Set("domain_scope_warning_accepted", "not-a-boolean");

  EXPECT_FALSE(DeserializeDeveloperProfile(*encoded));
}

TEST(DeveloperProfileIntegrationTest,
     RuntimeIdsNamespaceEqualAssetIdsByOwnerOrigin) {
  InMemoryDeveloperProfileStore store;
  const url::Origin first_owner =
      url::Origin::Create(GURL("https://one.example.test/"));
  const url::Origin second_owner =
      url::Origin::Create(GURL("https://two.example.test/"));
  DeveloperAsset shared{
      .id = "style-default",
      .name = "Shared domain style",
      .kind = DeveloperAssetKind::kStyle,
      .enabled = true,
      .source = "body { color: red; }",
      .scope = {.kind = DeveloperAssetScopeKind::kDomain,
                .value = "example.test"},
      .domain_scope_warning_accepted = true,
  };
  ASSERT_TRUE(store.Set(first_owner,
                        DeveloperProfile{.name = "First", .assets = {shared}}));
  ASSERT_TRUE(store.Set(
      second_owner, DeveloperProfile{.name = "Second", .assets = {shared}}));

  const GURL target("https://target.example.test/page");
  const std::vector<DeveloperAsset> first =
      GetDeveloperAssetsForNavigation(store, target);
  const std::vector<DeveloperAsset> second =
      GetDeveloperAssetsForNavigation(store, target);
  ASSERT_EQ(2u, first.size());
  ASSERT_EQ(2u, second.size());
  EXPECT_EQ("style-default", first[0].id);
  EXPECT_EQ("style-default", first[1].id);
  EXPECT_FALSE(first[0].runtime_id.empty());
  EXPECT_NE(first[0].runtime_id, first[1].runtime_id);
  EXPECT_EQ(first[0].runtime_id, second[0].runtime_id);
  EXPECT_EQ(first[1].runtime_id, second[1].runtime_id);
}

TEST(DeveloperProfileCodecTest, SyncIsExplicitAndKeychainRulesStayLocal) {
  DeveloperProfile profile = MakeProfile();
  profile.header_rules_sync_enabled = true;
  profile.header_rules.push_back({.name = "Authorization",
                                  .secret_reference = "ahoi-keychain:token",
                                  .action = DeveloperHeaderAction::kSet});
  profile.assets[1].source = "not-synced-script";
  profile.header_rules[0].value = "ordinary-value";

  std::optional<base::DictValue> payload =
      SerializeDeveloperProfileForSync(profile);
  ASSERT_TRUE(payload);
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(*payload, &json));
  EXPECT_NE(json.find("example-style"), std::string::npos);
  EXPECT_EQ(json.find("example-script"), std::string::npos);
  EXPECT_EQ(json.find("ahoi-keychain:token"), std::string::npos);
  EXPECT_EQ(json.find("resolved-secret"), std::string::npos);
}

TEST(DeveloperProfileCodecTest,
     SyncStripsLocalConsentAndKeepsAdvancedRulesDormant) {
  DeveloperProfile profile = MakeProfile();
  profile.response_header_rules_sync_enabled = true;
  profile.response_header_advanced_mode_acknowledged = true;
  profile.response_header_rules = {
      {.name = "Content-Security-Policy",
       .value = "default-src 'self'",
       .action = DeveloperHeaderAction::kSet},
      {.name = "X-Ahoi-Response",
       .value = "enabled",
       .action = DeveloperHeaderAction::kSet},
  };

  const std::optional<base::DictValue> payload =
      SerializeDeveloperProfileForSync(profile);
  ASSERT_TRUE(payload);
  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfile(*payload);
  ASSERT_TRUE(decoded);
  EXPECT_FALSE(decoded->response_header_advanced_mode_acknowledged);
  EXPECT_FALSE(decoded->response_header_rules_enabled);
  EXPECT_TRUE(decoded->response_header_rules_sync_enabled);
  EXPECT_EQ(decoded->response_header_rules, profile.response_header_rules);
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), *decoded),
            DeveloperProfileValidationError::kNone);
}

TEST(DeveloperProfileCodecTest,
     SyncStripsDomainScopeConsentAndKeepsSourceDormant) {
  DeveloperProfile profile = MakeProfile();
  profile.assets.resize(1);
  profile.assets[0].scope = {.kind = DeveloperAssetScopeKind::kDomain,
                             .value = "example.test"};
  profile.assets[0].domain_scope_warning_accepted = true;
  profile.assets[0].sync_enabled = true;

  const std::optional<base::DictValue> payload =
      SerializeDeveloperProfileForSync(profile);
  ASSERT_TRUE(payload);
  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfile(*payload);
  ASSERT_TRUE(decoded);
  ASSERT_EQ(1u, decoded->assets.size());
  EXPECT_FALSE(decoded->assets[0].enabled);
  EXPECT_FALSE(decoded->assets[0].domain_scope_warning_accepted);
  EXPECT_EQ(decoded->assets[0].source, profile.assets[0].source);
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), *decoded),
            DeveloperProfileValidationError::kNone);

  const std::optional<base::DictValue> trusted_local_payload =
      SerializeDeveloperProfile(profile);
  ASSERT_TRUE(trusted_local_payload);
  const std::optional<DeveloperProfile> ingress =
      DeserializeDeveloperProfileFromSync(*trusted_local_payload);
  ASSERT_TRUE(ingress);
  ASSERT_EQ(1u, ingress->assets.size());
  EXPECT_FALSE(ingress->assets[0].enabled);
  EXPECT_FALSE(ingress->assets[0].domain_scope_warning_accepted);
}

TEST(DeveloperProfileCodecTest,
     SyncIngressNeverTrustsTransferredAdvancedConsent) {
  DeveloperProfile profile = MakeProfile();
  profile.response_header_advanced_mode_acknowledged = true;
  profile.response_header_rules = {
      {.name = "Access-Control-Allow-Origin",
       .value = "*",
       .action = DeveloperHeaderAction::kSet},
  };
  const std::optional<base::DictValue> payload =
      SerializeDeveloperProfile(profile);
  ASSERT_TRUE(payload);

  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfileFromSync(*payload);
  ASSERT_TRUE(decoded);
  EXPECT_FALSE(decoded->response_header_advanced_mode_acknowledged);
  EXPECT_FALSE(decoded->response_header_rules_enabled);
  EXPECT_EQ(decoded->response_header_rules, profile.response_header_rules);
}

}  // namespace
}  // namespace ahoi
