// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_cookie_manager.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

GURL SiteUrl() {
  return GURL("https://app.example.com/dashboard");
}

TEST(DeveloperCookieManagerTest, NormalizesCurrentHostAndPath) {
  DeveloperCookieValidation result = ValidateDeveloperCookieDraft(
      SiteUrl(),
      DeveloperCookieDraft{
          .name = " session ", .value = "abc", .domain = " ", .path = " "},
      false);
  ASSERT_TRUE(result.valid());
  EXPECT_EQ("session", result.normalized.name);
  EXPECT_EQ("app.example.com", result.normalized.domain);
  EXPECT_EQ("/", result.normalized.path);
}

TEST(DeveloperCookieManagerTest, DomainCookiesMustMatchTheCurrentSite) {
  DeveloperCookieValidation parent = ValidateDeveloperCookieDraft(
      SiteUrl(),
      DeveloperCookieDraft{
          .name = "session", .value = "abc", .domain = ".example.com"},
      false);
  ASSERT_TRUE(parent.valid());
  EXPECT_EQ(".example.com", parent.normalized.domain);

  EXPECT_EQ(DeveloperCookieError::kInvalidDomain,
            ValidateDeveloperCookieDraft(
                SiteUrl(),
                DeveloperCookieDraft{
                    .name = "session", .value = "abc", .domain = "example.com"},
                false)
                .error);
  EXPECT_EQ(
      DeveloperCookieError::kInvalidDomain,
      ValidateDeveloperCookieDraft(
          SiteUrl(),
          DeveloperCookieDraft{
              .name = "session", .value = "abc", .domain = ".attacker.test"},
          false)
          .error);
}

TEST(DeveloperCookieManagerTest, RejectsUnsafeSameSiteAndPrefixCombinations) {
  EXPECT_EQ(
      DeveloperCookieError::kInvalidSameSite,
      ValidateDeveloperCookieDraft(
          SiteUrl(),
          DeveloperCookieDraft{.name = "session",
                               .value = "abc",
                               .domain = "app.example.com",
                               .secure = false,
                               .same_site = DeveloperCookieSameSite::kNone},
          false)
          .error);
  EXPECT_EQ(DeveloperCookieError::kInvalidPrefix,
            ValidateDeveloperCookieDraft(
                SiteUrl(),
                DeveloperCookieDraft{.name = "__Host-session",
                                     .value = "abc",
                                     .domain = ".example.com",
                                     .path = "/",
                                     .secure = true},
                false)
                .error);
  EXPECT_TRUE(ValidateDeveloperCookieDraft(
                  SiteUrl(),
                  DeveloperCookieDraft{.name = "__Host-session",
                                       .value = "abc",
                                       .domain = "app.example.com",
                                       .path = "/",
                                       .secure = true},
                  false)
                  .valid());
}

TEST(DeveloperCookieManagerTest, PartitionedCookiesRequireSecureContext) {
  DeveloperCookieDraft draft{.name = "chips",
                             .value = "value",
                             .domain = "app.example.com",
                             .partitioned = true};
  EXPECT_EQ(DeveloperCookieError::kInvalidPartitioned,
            ValidateDeveloperCookieDraft(SiteUrl(), draft, false).error);

  draft.secure = true;
  DeveloperCookieValidation valid =
      ValidateDeveloperCookieDraft(SiteUrl(), draft, false);
  ASSERT_TRUE(valid.valid());
  EXPECT_TRUE(valid.normalized.partitioned);

  EXPECT_EQ(DeveloperCookieError::kInvalidPartitioned,
            ValidateDeveloperCookieDraft(GURL("http://app.example.com/"), draft,
                                         false)
                .error);
}

TEST(DeveloperCookieManagerTest, KeepExpirationIsEditOnly) {
  DeveloperCookieDraft draft{.name = "session",
                             .value = "abc",
                             .domain = "app.example.com",
                             .expiration = DeveloperCookieExpiration::kKeep};
  EXPECT_EQ(DeveloperCookieError::kInvalidExpiration,
            ValidateDeveloperCookieDraft(SiteUrl(), draft, false).error);
  EXPECT_TRUE(ValidateDeveloperCookieDraft(SiteUrl(), draft, true).valid());
}

TEST(DeveloperCookieManagerTest, RejectsOversizedOrDelimitedPaths) {
  DeveloperCookieDraft draft{.name = "session",
                             .value = "abc",
                             .domain = "app.example.com",
                             .path = "/safe;other"};
  EXPECT_EQ(DeveloperCookieError::kInvalidPath,
            ValidateDeveloperCookieDraft(SiteUrl(), draft, false).error);

  draft.path = "/" + std::string(4096, 'x');
  EXPECT_EQ(DeveloperCookieError::kInvalidPath,
            ValidateDeveloperCookieDraft(SiteUrl(), draft, false).error);
}

TEST(DeveloperCookieManagerTest, SearchCoversEveryVisibleIdentityField) {
  DeveloperCookie cookie{.id = 7,
                         .name = "SessionToken",
                         .value = "alpha-value",
                         .domain = ".example.com",
                         .path = "/account"};
  EXPECT_TRUE(DeveloperCookieMatchesFilter(cookie, u"session"));
  EXPECT_TRUE(DeveloperCookieMatchesFilter(cookie, u"ALPHA"));
  EXPECT_TRUE(DeveloperCookieMatchesFilter(cookie, u"example"));
  EXPECT_TRUE(DeveloperCookieMatchesFilter(cookie, u"account"));
  EXPECT_FALSE(DeveloperCookieMatchesFilter(cookie, u"missing"));
}

}  // namespace
}  // namespace ahoi
