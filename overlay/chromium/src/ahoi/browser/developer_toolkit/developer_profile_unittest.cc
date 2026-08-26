// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_profile_codec.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_text_codec.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/developer_toolkit/developer_style_compiler.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/testing_pref_service.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
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

class FakeSecretStore final : public DeveloperSecretStore {
 public:
  std::optional<std::string> Store(std::string_view,
                                   std::string_view) override {
    return "ahoi-keychain:stored";
  }
  std::optional<std::string> Resolve(
      std::string_view reference) const override {
    return reference == "ahoi-keychain:token"
               ? std::optional<std::string>("resolved-secret")
               : std::nullopt;
  }
  bool Remove(std::string_view) override { return true; }
};

class FakeStyleCompilerService final : public DeveloperStyleCompilerService {
 public:
  explicit FakeStyleCompilerService(int* compile_count)
      : compile_count_(compile_count) {}

  void Compile(DeveloperStyleCompileRequest request,
               DeveloperStyleCompileCallback callback) override {
    ++*compile_count_;
    std::move(callback).Run(
        {.status = DeveloperStyleCompileStatus::kSucceeded,
         .css = request.language == DeveloperStyleLanguage::kLess
                    ? ".compiled { color: red; }"
                    : ".compiled { color: blue; }"});
  }

 private:
  raw_ptr<int> compile_count_;
};

TEST(DeveloperProfileValidationTest, RestrictsOriginsToHttpAndHttps) {
  const DeveloperProfile profile = MakeProfile();
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kNone);
  EXPECT_EQ(ValidateDeveloperProfile(
                url::Origin::Create(GURL("file:///tmp/index.html")), profile),
            DeveloperProfileValidationError::kUnsupportedOrigin);
  EXPECT_EQ(ValidateDeveloperProfile(
                url::Origin::Create(GURL("chrome://settings")), profile),
            DeveloperProfileValidationError::kUnsupportedOrigin);
}

TEST(DeveloperProfileValidationTest, RejectsUnsafeOrAmbiguousValues) {
  DeveloperProfile profile = MakeProfile();
  profile.assets[0].source.assign(kMaxDeveloperCssBytes + 1, 'x');
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kInvalidAsset);

  profile = MakeProfile();
  profile.header_rules[0].value = "one\r\ntwo";
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kHeaderValueInvalid);

  profile = MakeProfile();
  profile.header_rules.push_back({.name = "x-ahoi-mode",
                                  .value = "duplicate",
                                  .action = DeveloperHeaderAction::kSet});
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kDuplicateHeaderName);

  profile = MakeProfile();
  profile.header_rules[0].action = DeveloperHeaderAction::kRemove;
  profile.header_rules[0].value = "must-be-empty";
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kHeaderValueInvalid);

  profile = MakeProfile();
  profile.name.assign(kMaxDeveloperProfileNameBytes + 1, 'x');
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kInvalidProfileName);

  profile = MakeProfile();
  profile.assets[1].source.assign(kMaxDeveloperJavaScriptBytes + 1, 'x');
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kInvalidAsset);

  profile = MakeProfile();
  profile.user_agent.assign(kMaxDeveloperUserAgentBytes + 1, 'x');
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kUserAgentInvalid);

  profile = MakeProfile();
  profile.header_rules[0].name = "Not A Header";
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kHeaderNameInvalid);
}

TEST(DeveloperProfileCodecTest, RoundTripsExplicitOptInAndRules) {
  const DeveloperProfile expected = MakeProfile();
  std::optional<base::DictValue> encoded = SerializeDeveloperProfile(expected);
  ASSERT_TRUE(encoded);
  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfile(*encoded);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, expected);

  base::DictValue malformed = std::move(*encoded);
  malformed.Set("headers", base::DictValue().Set("enabled", true));
  EXPECT_FALSE(DeserializeDeveloperProfile(malformed));
}

TEST(DeveloperProfileCodecTest, MigratesV1FixedSlotsIntoScopedAssets) {
  std::optional<base::DictValue> encoded =
      SerializeDeveloperProfile(MakeProfile());
  ASSERT_TRUE(encoded);
  EXPECT_TRUE(encoded->Remove("assets"));
  encoded->Set("css", base::DictValue()
                          .Set("enabled", true)
                          .Set("source", "body { color: red; }"));
  encoded->Set("javascript",
               base::DictValue()
                   .Set("enabled", true)
                   .Set("source", "document.body.dataset.migrated = '1';"));

  const url::Origin origin = ExampleOrigin();
  const std::optional<DeveloperProfile> migrated =
      DeserializeDeveloperProfile(*encoded, &origin);
  ASSERT_TRUE(migrated);
  ASSERT_EQ(migrated->assets.size(), 2u);
  EXPECT_EQ(migrated->assets[0].style_language, DeveloperStyleLanguage::kCss);
  EXPECT_EQ(migrated->assets[0].scope.value, origin.Serialize());
  EXPECT_EQ(migrated->assets[1].javascript_world,
            DeveloperJavaScriptWorld::kIsolated);
}

TEST(DeveloperProfileCodecTest, MigratesProfilesWithoutResponseRules) {
  std::optional<base::DictValue> encoded =
      SerializeDeveloperProfile(MakeProfile());
  ASSERT_TRUE(encoded);
  EXPECT_TRUE(encoded->Remove("response_headers"));

  const std::optional<DeveloperProfile> decoded =
      DeserializeDeveloperProfile(*encoded);
  ASSERT_TRUE(decoded);
  EXPECT_FALSE(decoded->response_header_rules_enabled);
  EXPECT_TRUE(decoded->response_header_rules.empty());
  EXPECT_TRUE(decoded->header_rules_enabled);
  EXPECT_FALSE(decoded->header_rules.empty());
}

TEST(DeveloperProfileCodecTest, RejectsMalformedKnownResponseRules) {
  std::optional<base::DictValue> encoded =
      SerializeDeveloperProfile(MakeProfile());
  ASSERT_TRUE(encoded);
  encoded->Set("response_headers", base::ListValue());
  EXPECT_FALSE(DeserializeDeveloperProfile(*encoded));

  encoded = SerializeDeveloperProfile(MakeProfile());
  ASSERT_TRUE(encoded);
  encoded->Set("response_headers", base::DictValue().Set("enabled", true));
  EXPECT_FALSE(DeserializeDeveloperProfile(*encoded));
}

TEST(DeveloperProfileValidationTest,
     CountsRequestAndResponseRulesAgainstOneBoundedBudget) {
  DeveloperProfile profile = MakeProfile();
  profile.header_rules.clear();
  profile.response_header_rules.clear();
  for (size_t index = 0; index < kMaxDeveloperHeaderRules; ++index) {
    profile.header_rules.push_back(
        {.name = "X-Request-" + std::to_string(index),
         .value = "enabled",
         .action = DeveloperHeaderAction::kSet});
  }
  profile.response_header_rules.push_back(
      {.name = "X-One-Too-Many",
       .value = "enabled",
       .action = DeveloperHeaderAction::kSet});
  EXPECT_EQ(ValidateDeveloperProfile(ExampleOrigin(), profile),
            DeveloperProfileValidationError::kTooManyHeaderRules);
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

TEST(DeveloperAssetValidationTest, MatchesOriginDomainPathAndTabExplicitly) {
  DeveloperAsset asset = MakeProfile().assets[0];
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://example.test/next")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("https://sub.example.test/next")));

  asset.scope = {.kind = DeveloperAssetScopeKind::kDomain,
                 .value = "example.test"};
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://sub.example.test/next")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("http://sub.example.test/next")));

  asset.scope = {.kind = DeveloperAssetScopeKind::kPath, .value = "/docs/"};
  EXPECT_TRUE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                      GURL("https://example.test/docs/page")));
  EXPECT_FALSE(DoesDeveloperAssetMatch(ExampleOrigin(), asset,
                                       GURL("https://example.test/api/page")));
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

TEST(DeveloperSecretStoreTest, MaterializesAtomicallyWithoutPersistedSecret) {
  const FakeSecretStore secrets;
  const std::vector<DeveloperHeaderRule> rules = {
      {.name = "Authorization",
       .secret_reference = "ahoi-keychain:token",
       .action = DeveloperHeaderAction::kSet},
  };
  const auto materialized = MaterializeDeveloperHeaderRules(rules, secrets);
  ASSERT_TRUE(materialized);
  ASSERT_EQ(materialized->size(), 1u);
  EXPECT_EQ((*materialized)[0].value, "resolved-secret");
  EXPECT_TRUE((*materialized)[0].secret_reference.empty());

  std::vector<DeveloperHeaderRule> missing = rules;
  missing[0].secret_reference = "ahoi-keychain:missing";
  EXPECT_FALSE(MaterializeDeveloperHeaderRules(missing, secrets));
}

TEST(DeveloperStyleCompilerTest,
     LoadsOnlyForOpenPreprocessorEditorAndReleases) {
  int factory_count = 0;
  int compile_count = 0;
  LazyDeveloperStyleCompiler compiler(base::BindRepeating(
      [](int* factories, int* compiles) {
        ++*factories;
        return std::unique_ptr<DeveloperStyleCompilerService>(
            std::make_unique<FakeStyleCompilerService>(compiles));
      },
      &factory_count, &compile_count));
  DeveloperStyleCompileResult result;

  compiler.Compile(
      {.language = DeveloperStyleLanguage::kLess, .source = "@c: red;"},
      base::BindOnce(
          [](DeveloperStyleCompileResult* out,
             DeveloperStyleCompileResult value) { *out = std::move(value); },
          &result));
  EXPECT_EQ(result.status, DeveloperStyleCompileStatus::kEditorClosed);
  EXPECT_EQ(factory_count, 0);

  compiler.OpenEditor();
  compiler.Compile(
      {.language = DeveloperStyleLanguage::kCss,
       .source = "body { color: black; }"},
      base::BindOnce(
          [](DeveloperStyleCompileResult* out,
             DeveloperStyleCompileResult value) { *out = std::move(value); },
          &result));
  EXPECT_TRUE(result.succeeded());
  EXPECT_EQ(factory_count, 0);
  EXPECT_FALSE(compiler.service_loaded_for_testing());

  compiler.Compile(
      {.language = DeveloperStyleLanguage::kLess, .source = "@c: red;"},
      base::BindOnce(
          [](DeveloperStyleCompileResult* out,
             DeveloperStyleCompileResult value) { *out = std::move(value); },
          &result));
  EXPECT_TRUE(result.succeeded());
  EXPECT_EQ(factory_count, 1);
  EXPECT_EQ(compile_count, 1);
  EXPECT_TRUE(compiler.service_loaded_for_testing());
  compiler.CloseEditor();
  EXPECT_FALSE(compiler.service_loaded_for_testing());
}

TEST(DeveloperProfileStoreTest, PersistsExactOriginAndSchema) {
  TestingPrefServiceSimple prefs;
  developer_profile_prefs::RegisterProfilePrefs(prefs.registry());
  PrefDeveloperProfileStore store(&prefs, false);
  const DeveloperProfile profile = MakeProfile();

  ASSERT_TRUE(store.Set(ExampleOrigin(), profile));
  EXPECT_EQ(store.Get(ExampleOrigin()), profile);
  EXPECT_TRUE(store.Get(url::Origin::Create(
                  GURL("https://sub.example.test/path"))) == std::nullopt);
  ASSERT_EQ(store.ListOrigins().size(), 1u);
  EXPECT_EQ(store.ListOrigins()[0], ExampleOrigin());
  const base::DictValue& root = prefs.GetDict(kDeveloperProfilesPref);
  ASSERT_EQ(root.FindInt("version").value_or(-1),
            kDeveloperProfileSchemaVersion);
  EXPECT_TRUE(root.FindDict("origins"));

  EXPECT_TRUE(store.Remove(ExampleOrigin()));
  EXPECT_TRUE(store.ListOrigins().empty());
  EXPECT_FALSE(store.Remove(ExampleOrigin()));
}

TEST(DeveloperProfileStoreTest, IsFailClosedForIncognitoByDefault) {
  InMemoryDeveloperProfileStore incognito_store(/*is_off_the_record=*/true);
  EXPECT_FALSE(incognito_store.Set(ExampleOrigin(), MakeProfile()));
  EXPECT_FALSE(incognito_store.Get(ExampleOrigin()));
  EXPECT_TRUE(incognito_store.ListOrigins().empty());
}

TEST(DeveloperProfileStoreTest, SupportsExplicitEphemeralIncognitoOptIn) {
  InMemoryDeveloperProfileStore incognito_store(
      /*is_off_the_record=*/true, /*allow_incognito_overrides=*/true);
  ASSERT_TRUE(incognito_store.Set(ExampleOrigin(), MakeProfile()));
  EXPECT_TRUE(incognito_store.Get(ExampleOrigin()));
}

TEST(DeveloperProfileIntegrationTest, ResolvesAtNavigationBoundaryOnly) {
  InMemoryDeveloperProfileStore store;
  ASSERT_TRUE(store.Set(ExampleOrigin(), MakeProfile()));
  EXPECT_TRUE(GetDeveloperProfileForNavigation(
      store, GURL("https://example.test/next")));
  EXPECT_FALSE(
      GetDeveloperProfileForNavigation(store, GURL("chrome://settings")));
  EXPECT_FALSE(
      GetDeveloperProfileForNavigation(store, GURL("https://other.test/")));
}

TEST(DeveloperProfileTextCodecTest, ParsesSetRemoveCommentsAndWhitespace) {
  const DeveloperHeaderTextParseResult parsed = ParseDeveloperHeaderRules(
      "  # local testing\nX-Ahoi: enabled\n-X-Remove-Me\n\nAccept: text/plain "
      " ");
  ASSERT_TRUE(parsed.succeeded());
  ASSERT_EQ(parsed.rules.size(), 3u);
  EXPECT_EQ(parsed.rules[0],
            (DeveloperHeaderRule{.name = "X-Ahoi",
                                 .value = "enabled",
                                 .action = DeveloperHeaderAction::kSet}));
  EXPECT_EQ(parsed.rules[1],
            (DeveloperHeaderRule{.name = "X-Remove-Me",
                                 .value = "",
                                 .action = DeveloperHeaderAction::kRemove}));
  EXPECT_EQ(FormatDeveloperHeaderRules(parsed.rules),
            "X-Ahoi: enabled\n-X-Remove-Me\nAccept: text/plain");
}

TEST(DeveloperProfileTextCodecTest, ReportsPreciseInvalidLine) {
  DeveloperHeaderTextParseResult parsed =
      ParseDeveloperHeaderRules("X-One: 1\nmissing separator");
  EXPECT_EQ(parsed.error, DeveloperHeaderTextError::kMissingSeparator);
  EXPECT_EQ(parsed.error_line, 2u);

  parsed = ParseDeveloperHeaderRules("X-One: 1\nx-one: 2");
  EXPECT_EQ(parsed.error, DeveloperHeaderTextError::kDuplicateName);
  EXPECT_EQ(parsed.error_line, 2u);
}

TEST(DeveloperProfileTextCodecTest, RoundTripsOpaqueKeychainReferences) {
  const DeveloperHeaderTextParseResult parsed = ParseDeveloperHeaderRules(
      "Authorization: @keychain(ahoi-keychain:token)");
  ASSERT_TRUE(parsed.succeeded());
  ASSERT_EQ(parsed.rules.size(), 1u);
  EXPECT_TRUE(parsed.rules[0].value.empty());
  EXPECT_EQ(parsed.rules[0].secret_reference, "ahoi-keychain:token");
  EXPECT_EQ(FormatDeveloperHeaderRules(parsed.rules),
            "Authorization: @keychain(ahoi-keychain:token)");

  EXPECT_FALSE(ParseDeveloperHeaderRules(
                   "Authorization: @keychain(not-a-keychain-reference)")
                   .succeeded());
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     AppliesUserAgentAndSetAndRemoveRulesBeforeFirstRequest) {
  DeveloperProfile profile = MakeProfile();
  profile.header_rules = {
      {.name = "X-Ahoi", .value = "yes", .action = DeveloperHeaderAction::kSet},
      {.name = "X-Original",
       .value = "",
       .action = DeveloperHeaderAction::kRemove},
  };
  DeveloperProfileURLLoaderThrottle throttle(ExampleOrigin(), profile);
  network::ResourceRequest request;
  request.url = GURL("https://example.test/resource");
  request.headers.SetHeader("User-Agent", "Original/1.0");
  request.headers.SetHeader("X-Original", "keep elsewhere");
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);

  EXPECT_FALSE(defer);
  EXPECT_EQ(request.headers.GetHeader("User-Agent"), "AhoiDev/1.0");
  EXPECT_EQ(request.headers.GetHeader("X-Ahoi"), "yes");
  EXPECT_FALSE(request.headers.GetHeader("X-Original"));
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     AppliesResponseRulesOnlyForTheConfiguredOrigin) {
  DeveloperProfileURLLoaderThrottle throttle(ExampleOrigin(), MakeProfile());
  network::mojom::URLResponseHead head;
  head.headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nX-Remove-Response: old\r\n\r\n");
  ASSERT_TRUE(head.headers);
  bool defer = false;
  throttle.WillProcessResponse(GURL("https://example.test/resource"), &head,
                               &defer);
  EXPECT_FALSE(defer);
  EXPECT_EQ(head.headers->GetNormalizedHeader("X-Ahoi-Response"), "enabled");
  EXPECT_FALSE(head.headers->HasHeader("X-Remove-Response"));

  network::mojom::URLResponseHead cross_origin_head;
  cross_origin_head.headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nX-Remove-Response: keep\r\n\r\n");
  ASSERT_TRUE(cross_origin_head.headers);
  throttle.WillProcessResponse(GURL("https://other.test/resource"),
                               &cross_origin_head, &defer);
  EXPECT_FALSE(cross_origin_head.headers->HasHeader("X-Ahoi-Response"));
  EXPECT_TRUE(cross_origin_head.headers->HasHeader("X-Remove-Response"));
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     CacheOffBypassesReadsAndWritesWithoutBackgroundWork) {
  DeveloperProfile profile = MakeProfile();
  profile.user_agent_enabled = false;
  profile.header_rules_enabled = false;
  profile.response_header_rules_enabled = false;
  profile.cache_disabled = true;
  DeveloperProfileURLLoaderThrottle throttle(ExampleOrigin(), profile);
  network::ResourceRequest request;
  request.url = GURL("https://example.test/versioned.css");
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);
  EXPECT_FALSE(defer);
  EXPECT_TRUE(request.load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_TRUE(request.load_flags & net::LOAD_DISABLE_CACHE);
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     DisabledResponseRulesLeaveHeadersUntouched) {
  DeveloperProfile profile = MakeProfile();
  profile.response_header_rules_enabled = false;
  DeveloperProfileURLLoaderThrottle throttle(ExampleOrigin(), profile);
  network::mojom::URLResponseHead head;
  head.headers = net::HttpResponseHeaders::TryToCreate(
      "HTTP/1.1 200 OK\r\nX-Remove-Response: keep\r\n\r\n");
  ASSERT_TRUE(head.headers);
  bool defer = false;
  throttle.WillProcessResponse(GURL("https://example.test/resource"), &head,
                               &defer);
  EXPECT_FALSE(head.headers->HasHeader("X-Ahoi-Response"));
  EXPECT_TRUE(head.headers->HasHeader("X-Remove-Response"));
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     FactoryRequiresLiveWebContentsForResponseRules) {
  TestingPrefServiceSimple prefs;
  developer_profile_prefs::RegisterProfilePrefs(prefs.registry());
  PrefDeveloperProfileStore store(&prefs, false);
  DeveloperProfile profile = MakeProfile();
  profile.user_agent_enabled = false;
  profile.header_rules_enabled = false;
  profile.response_header_rules_enabled = true;
  ASSERT_TRUE(store.Set(ExampleOrigin(), profile));

  network::ResourceRequest request;
  request.url = GURL("https://example.test/path");
  request.is_outermost_main_frame = true;
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false, nullptr));

  profile.response_header_rules_enabled = false;
  ASSERT_TRUE(store.Set(ExampleOrigin(), profile));
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false, nullptr));
}

TEST(DeveloperProfileURLLoaderThrottleTest,
     FactoryFailsClosedWithoutLiveWebContents) {
  TestingPrefServiceSimple prefs;
  developer_profile_prefs::RegisterProfilePrefs(prefs.registry());
  PrefDeveloperProfileStore store(&prefs, false);
  ASSERT_TRUE(store.Set(ExampleOrigin(), MakeProfile()));

  network::ResourceRequest request;
  request.url = GURL("https://example.test/path");
  request.is_outermost_main_frame = true;
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false, nullptr));
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/true, nullptr));
  request.url = GURL("https://other.test/path");
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false, nullptr));
}

}  // namespace
}  // namespace ahoi
