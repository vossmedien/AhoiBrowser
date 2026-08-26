// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <string>
#include <utility>

#include "base/values.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_metadata.h"
#include "net/cookies/cookie_setting_override.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/cookie_settings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::privacy {
namespace {

ContentSettingPatternSource MakeSetting(
    std::string primary_pattern,
    std::string secondary_pattern,
    ContentSetting setting,
    content_settings::ProviderType provider =
        content_settings::ProviderType::kDefaultProvider) {
  return ContentSettingPatternSource(
      ContentSettingsPattern::FromString(primary_pattern),
      ContentSettingsPattern::FromString(secondary_pattern),
      base::Value(setting), provider, /*incognito=*/false,
      content_settings::RuleMetaData());
}

void ConfigureStrictCookieSettings(network::CookieSettings* settings) {
  settings->set_content_settings(
      ContentSettingsType::COOKIES,
      {MakeSetting("*", "*", CONTENT_SETTING_ALLOW)});
  settings->SetThirdPartyCookieBlockingPolicy(true, {});
}

TEST(PrivacyCookieEnforcementTest,
     StrictBlocksUnpartitionedButKeepsChipsAvailable) {
  network::CookieSettings settings;
  ConfigureStrictCookieSettings(&settings);
  const GURL request_url("https://third.test/resource");
  const GURL top_frame_url("https://top.test/");
  content_settings::CookieSettingsBase::CookieSettingWithMetadata metadata;

  EXPECT_FALSE(settings.IsFullCookieAccessAllowed(
      request_url, net::SiteForCookies(), url::Origin::Create(top_frame_url),
      net::CookieSettingOverrides(), std::nullopt, &metadata));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, metadata.cookie_setting());
  EXPECT_TRUE(metadata.BlockedByThirdPartyCookieBlocking());
  EXPECT_TRUE(metadata.allow_partitioned_cookies());
}

TEST(PrivacyCookieEnforcementTest,
     CompatibilityOverrideIsScopedToExactTopFrameOrigin) {
  network::CookieSettings settings;
  ConfigureStrictCookieSettings(&settings);
  settings.SetThirdPartyCookieBlockingPolicy(
      true, {{"https://compatible.test", false}});
  const GURL request_url("https://third.test/resource");

  EXPECT_TRUE(settings.IsFullCookieAccessAllowed(
      request_url, net::SiteForCookies(),
      url::Origin::Create(GURL("https://compatible.test/path")),
      net::CookieSettingOverrides(), std::nullopt, nullptr));
  EXPECT_FALSE(settings.IsFullCookieAccessAllowed(
      request_url, net::SiteForCookies(),
      url::Origin::Create(GURL("https://sub.compatible.test/")),
      net::CookieSettingOverrides(), std::nullopt, nullptr));
}

TEST(PrivacyCookieEnforcementTest,
     CompatibilityNeverWeakensChromiumCookieControls) {
  network::CookieSettings settings;
  ConfigureStrictCookieSettings(&settings);
  settings.set_block_third_party_cookies(true);
  settings.SetThirdPartyCookieBlockingPolicy(
      false, {{"https://compatible.test", false}});

  EXPECT_FALSE(settings.IsFullCookieAccessAllowed(
      GURL("https://third.test/resource"), net::SiteForCookies(),
      url::Origin::Create(GURL("https://compatible.test/")),
      net::CookieSettingOverrides(), std::nullopt, nullptr));
}

TEST(PrivacyCookieEnforcementTest, StorageAccessGrantStillTakesPrecedence) {
  network::CookieSettings settings;
  ConfigureStrictCookieSettings(&settings);
  const GURL request_url("https://third.test/resource");
  const GURL top_frame_url("https://top.test/");
  settings.set_content_settings(
      ContentSettingsType::STORAGE_ACCESS,
      {MakeSetting("https://third.test", "https://top.test",
                   CONTENT_SETTING_ALLOW,
                   content_settings::ProviderType::kPrefProvider)});
  net::CookieSettingOverrides overrides;
  overrides.Put(net::CookieSettingOverride::kStorageAccessGrantEligible);

  EXPECT_TRUE(settings.IsFullCookieAccessAllowed(
      request_url, net::SiteForCookies(), url::Origin::Create(top_frame_url),
      overrides, std::nullopt, nullptr));
}

TEST(PrivacyCookieEnforcementTest, EnterpriseCookieExceptionStillWins) {
  network::CookieSettings settings;
  ConfigureStrictCookieSettings(&settings);
  const GURL request_url("https://third.test/resource");
  const GURL top_frame_url("https://top.test/");
  settings.set_content_settings(
      ContentSettingsType::COOKIES,
      {MakeSetting("https://third.test", "https://top.test",
                   CONTENT_SETTING_ALLOW,
                   content_settings::ProviderType::kPolicyProvider),
       MakeSetting("*", "*", CONTENT_SETTING_ALLOW)});

  EXPECT_TRUE(settings.IsFullCookieAccessAllowed(
      request_url, net::SiteForCookies(), url::Origin::Create(top_frame_url),
      net::CookieSettingOverrides(), std::nullopt, nullptr));
}

}  // namespace
}  // namespace ahoi::privacy
