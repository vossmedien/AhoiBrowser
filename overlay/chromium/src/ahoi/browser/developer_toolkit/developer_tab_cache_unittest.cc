// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <memory>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

class DeveloperTabCacheTest : public testing::Test {
 protected:
  DeveloperTabCacheTest() {
    developer_toolkit_prefs::RegisterProfilePrefs(prefs_.registry());
    prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
    user_prefs::UserPrefs::Set(&context_, &prefs_);
    first_ = content::WebContentsTester::CreateTestWebContents(&context_, nullptr);
    second_ = content::WebContentsTester::CreateTestWebContents(&context_, nullptr);
    content::WebContentsTester::For(first_.get())->NavigateAndCommit(url_);
    content::WebContentsTester::For(second_.get())->NavigateAndCommit(url_);
  }
  const GURL url_{"https://example.test/docs"};
  content::BrowserTaskEnvironment environment_;
  content::RenderViewHostTestEnabler renderer_;
  TestingPrefServiceSimple prefs_;
  content::TestBrowserContext context_;
  std::unique_ptr<content::WebContents> first_;
  std::unique_ptr<content::WebContents> second_;
};

TEST_F(DeveloperTabCacheTest, SameOriginPeerAndSavedEditorStayUnchanged) {
  DeveloperProfileTabHelper first(first_.get(), &prefs_);
  DeveloperProfileTabHelper second(second_.get(), &prefs_);
  const auto origin = url::Origin::Create(url_);
  const DeveloperProfile saved{.name = "Saved site profile",
                               .user_agent = "Synthetic dormant UA"};
  ASSERT_TRUE(first.SaveProfile(origin, saved));
  const auto before = prefs_.GetDict(kDeveloperProfilesPref).Clone();
  ASSERT_TRUE(first.SetCacheDisabledForCurrentTab(true));
  ASSERT_TRUE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_));
  EXPECT_TRUE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_)->cache_disabled);
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&prefs_, second_.get(), url_)->cache_disabled);
  EXPECT_FALSE(first.GetProfile(origin)->cache_disabled);
  ASSERT_TRUE(first.SaveProfile(origin, *first.GetProfile(origin)));
  EXPECT_EQ(before, prefs_.GetDict(kDeveloperProfilesPref));
  EXPECT_TRUE(first.IsCacheDisabledForCurrentTab());
}

TEST_F(DeveloperTabCacheTest, NoOriginProfileNeededAndForeignPrefsRejected) {
  DeveloperProfileTabHelper helper(first_.get(), &prefs_);
  ASSERT_TRUE(helper.SetCacheDisabledForCurrentTab(true));
  EXPECT_TRUE(prefs_.GetDict(kDeveloperProfilesPref).empty());
  EXPECT_FALSE(GetDeveloperProfileForTab(&prefs_, first_.get(), url_));
  EXPECT_TRUE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_)->cache_disabled);
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&prefs_, second_.get(), url_));
  TestingPrefServiceSimple foreign;
  developer_toolkit_prefs::RegisterProfilePrefs(foreign.registry());
  foreign.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&foreign, first_.get(), url_));
}

TEST_F(DeveloperTabCacheTest, MasterDisableAndResetRetireOldGeneration) {
  DeveloperProfileTabHelper helper(first_.get(), &prefs_);
  ASSERT_TRUE(helper.SetCacheDisabledForCurrentTab(true));
  const auto enabled = helper.activation_generation();
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  EXPECT_FALSE(helper.IsCacheDisabledForCurrentTab());
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_));
  EXPECT_FALSE(helper.SetCacheDisabledForCurrentTab(true));
  EXPECT_GT(helper.activation_generation(), enabled);
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  EXPECT_TRUE(helper.IsCacheDisabledForCurrentTab());
  const auto reset = helper.activation_generation();
  ASSERT_TRUE(helper.ResetProfilesForUrl(url_));
  EXPECT_FALSE(helper.IsCacheDisabledForCurrentTab());
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_));
  EXPECT_GT(helper.activation_generation(), reset);
}

TEST_F(DeveloperTabCacheTest, TabReplacementDropsChoiceAndPersistentSiteStillWins) {
  DeveloperProfileTabHelper helper(first_.get(), &prefs_);
  ASSERT_TRUE(helper.SetCacheDisabledForCurrentTab(true));
  helper.SetWebContents(second_.get());
  EXPECT_FALSE(helper.IsCacheDisabledForCurrentTab());
  EXPECT_FALSE(GetDeveloperNetworkProfileForTab(&prefs_, second_.get(), url_));
  DeveloperProfile persistent{.name = "Explicit origin cache setting"};
  persistent.cache_disabled = true;
  ASSERT_TRUE(helper.SaveProfile(url::Origin::Create(url_), persistent));
  ASSERT_TRUE(helper.SetCacheDisabledForCurrentTab(true));
  ASSERT_TRUE(helper.SetCacheDisabledForCurrentTab(false));
  EXPECT_TRUE(GetDeveloperNetworkProfileForTab(&prefs_, second_.get(), url_)->cache_disabled);
  EXPECT_TRUE(GetDeveloperNetworkProfileForTab(&prefs_, first_.get(), url_)->cache_disabled);
}

}  // namespace
}  // namespace ahoi
