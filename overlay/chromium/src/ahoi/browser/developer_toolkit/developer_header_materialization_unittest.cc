// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "base/functional/bind.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/site_instance.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/mock_navigation_throttle_registry.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "content/test/test_web_contents.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/frame/frame_policy.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi {
namespace {

constexpr char kRequestSecretReference[] = "ahoi-keychain:request-token";
constexpr char kResponseSecretReference[] = "ahoi-keychain:response-token";
constexpr char kRequestSecret[] = "Bearer request-secret";
constexpr char kResponseSecret[] = "response-secret";

url::Origin TestOrigin() {
  return url::Origin::Create(GURL("https://headers.example.test/"));
}

DeveloperProfile MakeSecretHeaderProfile() {
  DeveloperProfile profile;
  profile.name = "Secret headers";
  profile.header_rules_enabled = true;
  profile.header_rules.push_back({.name = "Authorization",
                                  .secret_reference = kRequestSecretReference,
                                  .action = DeveloperHeaderAction::kSet});
  profile.response_header_rules_enabled = true;
  profile.response_header_rules.push_back(
      {.name = "X-Ahoi-Response-Secret",
       .secret_reference = kResponseSecretReference,
       .action = DeveloperHeaderAction::kSet});
  return profile;
}

class FixedDeveloperSecretStore final : public DeveloperSecretStore {
 public:
  explicit FixedDeveloperSecretStore(bool resolve_response)
      : resolve_response_(resolve_response) {}

  std::optional<std::string> Store(std::string_view,
                                   std::string_view) override {
    return std::nullopt;
  }

  std::optional<std::string> Resolve(
      std::string_view reference) const override {
    if (reference == kRequestSecretReference) {
      return kRequestSecret;
    }
    if (resolve_response_ && reference == kResponseSecretReference) {
      return kResponseSecret;
    }
    return std::nullopt;
  }

  bool Remove(std::string_view) override { return false; }

 private:
  const bool resolve_response_;
};

DeveloperSecretStoreFactory SecretStoreFactory(bool resolve_response) {
  return base::BindRepeating(
      [](bool should_resolve_response) {
        return std::unique_ptr<DeveloperSecretStore>(
            std::make_unique<FixedDeveloperSecretStore>(
                should_resolve_response));
      },
      resolve_response);
}

network::ResourceRequest MainFrameRequest(const GURL& url) {
  network::ResourceRequest request;
  request.url = url;
  request.is_outermost_main_frame = true;
  request.navigation_redirect_chain.push_back(url);
  return request;
}

class DestroyingTestWebContents final : public content::TestWebContents {
 public:
  explicit DestroyingTestWebContents(content::BrowserContext* browser_context)
      : content::TestWebContents(browser_context) {}
  ~DestroyingTestWebContents() override = default;

  static std::unique_ptr<DestroyingTestWebContents> Create(
      content::BrowserContext* browser_context) {
    auto result = std::make_unique<DestroyingTestWebContents>(browser_context);
    result->Init(
        content::WebContents::CreateParams(
            browser_context, content::SiteInstance::Create(browser_context)),
        blink::FramePolicy());
    result->is_being_destroyed_for_testing_ = true;
    return result;
  }

  bool IsBeingDestroyed() override { return is_being_destroyed_for_testing_; }

 private:
  bool is_being_destroyed_for_testing_ = false;
};

class DeveloperHeaderMaterializationTest : public testing::Test {
 public:
  DeveloperHeaderMaterializationTest() = default;

  void SetUp() override {
    developer_profile_prefs::RegisterProfilePrefs(prefs_.registry());
    user_prefs::UserPrefs::Set(&browser_context_, &prefs_);
    web_contents_ = content::WebContentsTester::CreateTestWebContents(
        &browser_context_, nullptr);
  }

  void SaveProfile(const DeveloperProfile& profile) {
    PrefDeveloperProfileStore store(&prefs_, /*is_off_the_record=*/false);
    ASSERT_TRUE(store.Set(TestOrigin(), profile));
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_test_enabler_;
  TestingPrefServiceSimple prefs_;
  content::TestBrowserContext browser_context_;
  std::unique_ptr<content::WebContents> web_contents_;
};

TEST_F(DeveloperHeaderMaterializationTest,
       ResolvesRequestAndResponseAsOneTransaction) {
  const DeveloperProfile source = MakeSecretHeaderProfile();
  const FixedDeveloperSecretStore complete_store(/*resolve_response=*/true);
  std::optional<DeveloperProfile> materialized =
      MaterializeDeveloperProfileHeaderSecrets(source, complete_store);
  ASSERT_TRUE(materialized);
  ASSERT_EQ(materialized->header_rules.size(), 1u);
  ASSERT_EQ(materialized->response_header_rules.size(), 1u);
  EXPECT_EQ(materialized->header_rules[0].value, kRequestSecret);
  EXPECT_TRUE(materialized->header_rules[0].secret_reference.empty());
  EXPECT_EQ(materialized->response_header_rules[0].value, kResponseSecret);
  EXPECT_TRUE(materialized->response_header_rules[0].secret_reference.empty());

  const FixedDeveloperSecretStore incomplete_store(
      /*resolve_response=*/false);
  EXPECT_FALSE(
      MaterializeDeveloperProfileHeaderSecrets(source, incomplete_store));
  EXPECT_EQ(source.header_rules[0].secret_reference, kRequestSecretReference);
  EXPECT_EQ(source.response_header_rules[0].secret_reference,
            kResponseSecretReference);
}

TEST_F(DeveloperHeaderMaterializationTest,
       DefersExactNavigationAndKeepsPlaintextRequestBound) {
  const DeveloperProfile profile = MakeSecretHeaderProfile();
  SaveProfile(profile);
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  auto navigation_throttle =
      std::make_unique<DeveloperProfileNavigationThrottle>(
          registry, &prefs_, SecretStoreFactory(/*resolve_response=*/true));
  int resume_count = 0;
  navigation_throttle->set_resume_callback_for_testing(
      base::BindRepeating([](int* count) { ++*count; }, &resume_count));

  EXPECT_EQ(content::NavigationThrottle::DEFER,
            navigation_throttle->WillStartRequest().action());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(resume_count, 1);

  network::ResourceRequest request = MainFrameRequest(url);
  std::unique_ptr<blink::URLLoaderThrottle> request_throttle =
      MaybeCreateDeveloperProfileURLLoaderThrottle(
          request, &prefs_, /*is_off_the_record=*/false, web_contents_.get());
  ASSERT_TRUE(request_throttle);
  bool defer = false;
  request_throttle->WillStartRequest(&request, &defer);
  EXPECT_FALSE(defer);
  EXPECT_EQ(request.headers.GetHeader("Authorization"), kRequestSecret);

  network::mojom::URLResponseHead response;
  response.headers =
      net::HttpResponseHeaders::TryToCreate("HTTP/1.1 200 OK\r\n\r\n");
  ASSERT_TRUE(response.headers);
  request_throttle->WillProcessResponse(url, &response, &defer);
  EXPECT_EQ(response.headers->GetNormalizedHeader("X-Ahoi-Response-Secret"),
            kResponseSecret);

  PrefDeveloperProfileStore persisted_store(&prefs_,
                                            /*is_off_the_record=*/false);
  const std::optional<DeveloperProfile> persisted =
      persisted_store.Get(TestOrigin());
  ASSERT_TRUE(persisted);
  EXPECT_EQ(persisted->header_rules[0].secret_reference,
            kRequestSecretReference);
  EXPECT_TRUE(persisted->header_rules[0].value.empty());

  navigation_throttle.reset();
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(url), &prefs_, /*is_off_the_record=*/false,
      web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       MissingResponseSecretDisablesWholeHeaderProfile) {
  SaveProfile(MakeSecretHeaderProfile());
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  DeveloperProfileNavigationThrottle navigation_throttle(
      registry, &prefs_, SecretStoreFactory(/*resolve_response=*/false));
  navigation_throttle.set_resume_callback_for_testing(
      base::BindRepeating([] {}));

  EXPECT_EQ(content::NavigationThrottle::DEFER,
            navigation_throttle.WillStartRequest().action());
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(url), &prefs_, /*is_off_the_record=*/false,
      web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       FactoryRejectsNullDestroyedStaleOtrAndNavigationMismatch) {
  DeveloperProfile plain_profile;
  plain_profile.name = "Plain headers";
  plain_profile.header_rules_enabled = true;
  plain_profile.header_rules.push_back({.name = "X-Ahoi", .value = "enabled"});
  SaveProfile(plain_profile);
  const GURL url = TestOrigin().GetURL();
  network::ResourceRequest request = MainFrameRequest(url);

  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/false, nullptr));

  std::unique_ptr<DestroyingTestWebContents> destroying_contents =
      DestroyingTestWebContents::Create(&browser_context_);
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/false,
      destroying_contents.get()));

  TestingPrefServiceSimple stale_prefs;
  content::TestBrowserContext stale_context;
  developer_profile_prefs::RegisterProfilePrefs(stale_prefs.registry());
  user_prefs::UserPrefs::Set(&stale_context, &stale_prefs);
  std::unique_ptr<content::WebContents> stale_contents =
      content::WebContentsTester::CreateTestWebContents(&stale_context,
                                                        nullptr);
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/false, stale_contents.get()));

  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/true, web_contents_.get()));
  browser_context_.set_is_off_the_record(true);
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/false, web_contents_.get()));
  browser_context_.set_is_off_the_record(false);

  request.navigation_redirect_chain = {
      GURL("https://different-navigation.example/")};
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      request, &prefs_, /*is_off_the_record=*/false, web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       NavigationThrottleDoesNotReadOrApplyAnOtrProfile) {
  DeveloperProfile profile = MakeSecretHeaderProfile();
  profile.user_agent_enabled = true;
  profile.user_agent = "Ahoi-Test-Agent";
  SaveProfile(profile);
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  DeveloperProfileNavigationThrottle navigation_throttle(
      registry, &prefs_, SecretStoreFactory(/*resolve_response=*/true));
  browser_context_.set_is_off_the_record(true);

  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_throttle.WillStartRequest().action());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(web_contents_->GetUserAgentOverride().ua_string_override.empty());
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(url), &prefs_, /*is_off_the_record=*/false,
      web_contents_.get()));
  browser_context_.set_is_off_the_record(false);
}

TEST_F(DeveloperHeaderMaterializationTest,
       SubresourceOriginMismatchAndSecretsStayFailClosed) {
  const DeveloperProfile secret_profile = MakeSecretHeaderProfile();
  UpdateDeveloperProfileNetworkState(*web_contents_, TestOrigin().GetURL(),
                                     secret_profile);
  network::ResourceRequest same_origin;
  same_origin.url = GURL("https://headers.example.test/app.js");
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      same_origin, &prefs_, /*is_off_the_record=*/false, web_contents_.get()));

  DeveloperProfile plain_profile;
  plain_profile.header_rules_enabled = true;
  plain_profile.header_rules.push_back({.name = "X-Ahoi", .value = "enabled"});
  UpdateDeveloperProfileNetworkState(*web_contents_, TestOrigin().GetURL(),
                                     plain_profile);
  network::ResourceRequest other_origin;
  other_origin.url = GURL("https://other.example.test/app.js");
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      other_origin, &prefs_, /*is_off_the_record=*/false, web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       NavigationChangeBeforeReplyCannotStageStaleSecrets) {
  SaveProfile(MakeSecretHeaderProfile());
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  DeveloperProfileNavigationThrottle navigation_throttle(
      registry, &prefs_, SecretStoreFactory(/*resolve_response=*/true));
  navigation_throttle.set_resume_callback_for_testing(
      base::BindRepeating([] {}));

  EXPECT_EQ(content::NavigationThrottle::DEFER,
            navigation_throttle.WillStartRequest().action());
  handle.set_url(GURL("https://changed.example.test/"));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(url), &prefs_, /*is_off_the_record=*/false,
      web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       ProfileMutationAfterMaterializationInvalidatesSnapshot) {
  DeveloperProfile profile = MakeSecretHeaderProfile();
  SaveProfile(profile);
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  DeveloperProfileNavigationThrottle navigation_throttle(
      registry, &prefs_, SecretStoreFactory(/*resolve_response=*/true));
  navigation_throttle.set_resume_callback_for_testing(
      base::BindRepeating([] {}));
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            navigation_throttle.WillStartRequest().action());
  task_environment_.RunUntilIdle();

  profile.header_rules[0].secret_reference = "ahoi-keychain:rotated-token";
  SaveProfile(profile);
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(url), &prefs_, /*is_off_the_record=*/false,
      web_contents_.get()));
}

TEST_F(DeveloperHeaderMaterializationTest,
       SameOriginRedirectRetargetsAndCrossOriginRedirectClears) {
  SaveProfile(MakeSecretHeaderProfile());
  const GURL url = TestOrigin().GetURL();
  testing::NiceMock<content::MockNavigationHandle> handle(
      url, web_contents_->GetPrimaryMainFrame());
  content::MockNavigationThrottleRegistry registry(
      &handle,
      content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
  DeveloperProfileNavigationThrottle navigation_throttle(
      registry, &prefs_, SecretStoreFactory(/*resolve_response=*/true));
  navigation_throttle.set_resume_callback_for_testing(
      base::BindRepeating([] {}));
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            navigation_throttle.WillStartRequest().action());
  task_environment_.RunUntilIdle();

  const GURL same_origin_redirect("https://headers.example.test/redirected");
  handle.set_url(same_origin_redirect);
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_throttle.WillRedirectRequest().action());
  EXPECT_TRUE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(same_origin_redirect), &prefs_,
      /*is_off_the_record=*/false, web_contents_.get()));

  handle.set_url(GURL("https://cross-origin.example.test/"));
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            navigation_throttle.WillRedirectRequest().action());
  EXPECT_FALSE(MaybeCreateDeveloperProfileURLLoaderThrottle(
      MainFrameRequest(same_origin_redirect), &prefs_,
      /*is_off_the_record=*/false, web_contents_.get()));
}

}  // namespace
}  // namespace ahoi
