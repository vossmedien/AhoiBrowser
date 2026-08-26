// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_url_loader_throttle.h"

#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

TEST(PrivacyModeURLLoaderThrottleTest, AddsGpcAndStripsMainFrameQuery) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));
  network::ResourceRequest request;
  request.url =
      GURL("https://example.test/?utm_campaign=one&keep=two&fbclid=three");
  request.is_outermost_main_frame = true;
  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  ASSERT_TRUE(throttle);
  bool defer = false;
  throttle->WillStartRequest(&request, &defer);
  EXPECT_EQ("1", request.headers.GetHeader("Sec-GPC").value_or(""));
  EXPECT_EQ("https://example.test/?keep=two", request.url.spec());
}

TEST(PrivacyModeURLLoaderThrottleTest, DoesNotStripSubresourceQuery) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));
  network::ResourceRequest request;
  request.url = GURL("https://cdn.test/script.js?utm_source=one&keep=two");
  request.request_initiator = url::Origin::Create(GURL("https://site.test/"));
  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  ASSERT_TRUE(throttle);
  bool defer = false;
  throttle->WillStartRequest(&request, &defer);
  EXPECT_EQ("1", request.headers.GetHeader("Sec-GPC").value_or(""));
  EXPECT_EQ("https://cdn.test/script.js?utm_source=one&keep=two",
            request.url.spec());
}

TEST(PrivacyModeURLLoaderThrottleTest,
     StrictModeReducesOnlyThirdPartyHighEntropyUaHints) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));

  network::ResourceRequest request;
  request.url = GURL("https://third-party.test/script.js");
  request.request_initiator = url::Origin::Create(GURL("https://site.test/"));
  request.site_for_cookies =
      net::SiteForCookies::FromUrl(GURL("https://site.test/"));
  request.headers.SetHeader("Sec-CH-UA", "low-entropy-brand");
  request.headers.SetHeader("Sec-CH-UA-Arch", "arm");
  request.headers.SetHeader("Sec-CH-UA-Full-Version-List", "full-version");
  request.headers.SetHeader("Sec-CH-UA-Platform-Version", "26.0");

  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  ASSERT_TRUE(throttle);
  bool defer = false;
  throttle->WillStartRequest(&request, &defer);

  EXPECT_TRUE(request.headers.HasHeader("Sec-CH-UA"));
  EXPECT_FALSE(request.headers.HasHeader("Sec-CH-UA-Arch"));
  EXPECT_FALSE(request.headers.HasHeader("Sec-CH-UA-Full-Version-List"));
  EXPECT_FALSE(request.headers.HasHeader("Sec-CH-UA-Platform-Version"));
}

TEST(PrivacyModeURLLoaderThrottleTest,
     StrictModePreservesFirstPartyHighEntropyUaHints) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));

  network::ResourceRequest request;
  request.url = GURL("https://site.test/script.js");
  request.request_initiator = url::Origin::Create(GURL("https://site.test/"));
  request.site_for_cookies =
      net::SiteForCookies::FromUrl(GURL("https://site.test/"));
  request.headers.SetHeader("Sec-CH-UA-Arch", "arm");

  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  ASSERT_TRUE(throttle);
  bool defer = false;
  throttle->WillStartRequest(&request, &defer);

  EXPECT_TRUE(request.headers.HasHeader("Sec-CH-UA-Arch"));
}

TEST(PrivacyModeURLLoaderThrottleTest, CompatibilityExceptionIsNoOp) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));
  ASSERT_TRUE(SetOriginMode(&prefs, GURL("https://example.test/"),
                            PrivacyMode::kChromiumCompatible, false));
  network::ResourceRequest request;
  request.url = GURL("https://example.test/?utm_source=one");
  request.is_outermost_main_frame = true;
  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  EXPECT_FALSE(throttle);
}

TEST(PrivacyModeURLLoaderThrottleTest, RedirectToCompatibilityRemovesGpc) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  ASSERT_TRUE(SetGlobalMode(&prefs, PrivacyMode::kStrict));
  ASSERT_TRUE(SetOriginMode(&prefs, GURL("https://other.test/"),
                            PrivacyMode::kChromiumCompatible, false));
  network::ResourceRequest request;
  request.url = GURL("https://example.test/");
  request.is_outermost_main_frame = true;
  auto throttle = MaybeCreatePrivacyModeURLLoaderThrottle(
      request, &prefs, /*is_off_the_record=*/false);
  ASSERT_TRUE(throttle);

  net::RedirectInfo redirect_info;
  redirect_info.new_url = GURL("https://other.test/");
  network::mojom::URLResponseHead response_head;
  bool defer = false;
  network::HttpRequestHeadersUpdateParams updates;
  throttle->WillRedirectRequest(&redirect_info, response_head, &defer,
                                &updates);
  EXPECT_EQ(1u, updates.removed_headers.size());
  EXPECT_EQ("Sec-GPC", updates.removed_headers.front());
}

}  // namespace
}  // namespace ahoi::privacy
