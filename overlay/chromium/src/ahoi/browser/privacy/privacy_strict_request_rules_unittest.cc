// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_strict_request_rules.h"

#include "net/cookies/site_for_cookies.h"
#include "services/network/public/cpp/resource_request.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

namespace ahoi::privacy {
namespace {

network::ResourceRequest Subresource(const char* url, const char* top) {
  network::ResourceRequest request;
  request.url = GURL(url);
  request.site_for_cookies =
      net::SiteForCookies::FromOrigin(url::Origin::Create(GURL(top)));
  request.request_initiator = url::Origin::Create(GURL(top));
  return request;
}

// Handoff 066: a third-party image of a strict page (the fixture's /pixel).
TEST(PrivacyStrictRequestRulesTest, ThirdPartySubresourceGetsAllRules) {
  network::ResourceRequest request =
      Subresource("https://tracker.test/pixel", "https://site.test/");
  request.referrer = GURL("https://site.test/top?secret=1");
  request.referrer_policy = net::ReferrerPolicy::NEVER_CLEAR;  // unsafe-url
  request.headers.SetHeader("Sec-CH-UA", "\"Chromium\";v=\"153\"");
  request.headers.SetHeader("Sec-CH-UA-Model", "\"Mac\"");
  ApplyStrictRequestRules(request, /*is_main_frame=*/false);
  EXPECT_EQ("1", request.headers.GetHeader("Sec-GPC").value_or(""));
  EXPECT_EQ(GURL("https://site.test/"), request.referrer);
  EXPECT_EQ(net::ReferrerPolicy::ORIGIN, request.referrer_policy);
  EXPECT_FALSE(request.headers.HasHeader("Sec-CH-UA-Model"));
  EXPECT_TRUE(request.headers.HasHeader("Sec-CH-UA"));  // low entropy stays
}

TEST(PrivacyStrictRequestRulesTest, FirstPartyKeepsHintsAndFullReferrer) {
  network::ResourceRequest request =
      Subresource("https://site.test/app.js", "https://site.test/");
  request.referrer = GURL("https://site.test/top?page=2");
  request.referrer_policy = net::ReferrerPolicy::NEVER_CLEAR;
  request.headers.SetHeader("Sec-CH-UA-Model", "\"Mac\"");
  ApplyStrictRequestRules(request, /*is_main_frame=*/false);
  EXPECT_TRUE(request.headers.HasHeader("Sec-CH-UA-Model"));
  EXPECT_EQ(GURL("https://site.test/top?page=2"), request.referrer);
  // Capped, so a later cross-site redirect only carries the origin.
  EXPECT_EQ(net::ReferrerPolicy::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN,
            request.referrer_policy);
}

TEST(PrivacyStrictRequestRulesTest, CapKeepsStricterPolicies) {
  EXPECT_EQ(net::ReferrerPolicy::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN,
            CapReferrerPolicyForStrictMode(
                net::ReferrerPolicy::CLEAR_ON_TRANSITION_FROM_SECURE_TO_INSECURE));
  EXPECT_EQ(net::ReferrerPolicy::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN,
            CapReferrerPolicyForStrictMode(
                net::ReferrerPolicy::ORIGIN_ONLY_ON_TRANSITION_CROSS_ORIGIN));
  EXPECT_EQ(net::ReferrerPolicy::NO_REFERRER,
            CapReferrerPolicyForStrictMode(net::ReferrerPolicy::NO_REFERRER));
  EXPECT_EQ(net::ReferrerPolicy::CLEAR_ON_TRANSITION_CROSS_ORIGIN,
            CapReferrerPolicyForStrictMode(
                net::ReferrerPolicy::CLEAR_ON_TRANSITION_CROSS_ORIGIN));
}

}  // namespace
}  // namespace ahoi::privacy
