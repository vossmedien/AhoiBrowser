// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_action_executor.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

class DeveloperProfileRuntimeTest : public testing::Test {
 protected:
  DeveloperProfileRuntimeTest() {
    developer_profile_prefs::RegisterProfilePrefs(prefs_.registry());
    web_contents_ = content::WebContentsTester::CreateTestWebContents(
        &browser_context_, nullptr);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext browser_context_;
  TestingPrefServiceSimple prefs_;
  std::unique_ptr<content::WebContents> web_contents_;
};

DeveloperAsset Asset(std::string id,
                     DeveloperAssetLifetime lifetime,
                     DeveloperAssetScope scope,
                     DeveloperAssetKind kind = DeveloperAssetKind::kJavaScript,
                     bool enabled = true) {
  const bool domain_scope = scope.kind == DeveloperAssetScopeKind::kDomain;
  return {.id = std::move(id),
          .name = "Runtime asset",
          .kind = kind,
          .enabled = enabled,
          .source = kind == DeveloperAssetKind::kStyle
                        ? "body { color: rgb(1, 2, 3); }"
                        : "globalThis.ahoiRuntime = true;",
          .scope = std::move(scope),
          .domain_scope_warning_accepted = domain_scope,
          .lifetime = lifetime};
}

class RejectingDataAdapter final : public BrowsingDataRemovalAdapter {
 public:
  bool Remove(const BrowsingDataClearRequest&, CompletionCallback) override {
    return false;
  }
};

class AllowSettingsAdapter final : public ContentSettingsAdapter {
 public:
  std::optional<ContentSettingValue> Get(const url::Origin&,
                                         ContentSettingType) const override {
    return ContentSettingValue::kAllow;
  }
  bool Set(const url::Origin&,
           ContentSettingType,
           ContentSettingValue) override {
    return true;
  }
  bool Reset(const url::Origin&, ContentSettingType) override { return true; }
};

std::unique_ptr<DeveloperActionExecutor> CreateActionExecutor() {
  return std::make_unique<DeveloperActionExecutor>(
      std::make_unique<RejectingDataAdapter>(),
      std::make_unique<AllowSettingsAdapter>());
}

TEST_F(DeveloperProfileRuntimeTest,
       KeepsTransientAssetsInTabAndConsumesOnceAtNavigation) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  EXPECT_EQ(&helper,
            DeveloperProfileTabHelper::FromWebContents(web_contents_.get()));
  ASSERT_FALSE(helper.tab_token().empty());

  const GURL url("https://example.test/docs/page");
  const url::Origin origin = url::Origin::Create(url);
  DeveloperProfile profile{.name = "Runtime profile"};
  profile.assets.push_back(Asset(
      "once", DeveloperAssetLifetime::kOnce,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = origin.Serialize()}));
  profile.assets.push_back(Asset("reload", DeveloperAssetLifetime::kReload,
                                 {.kind = DeveloperAssetScopeKind::kCurrentTab,
                                  .value = helper.tab_token()}));
  profile.assets.push_back(
      Asset("restart", DeveloperAssetLifetime::kRestart,
            {.kind = DeveloperAssetScopeKind::kPath, .value = "/docs/"}));

  ASSERT_TRUE(helper.SaveProfile(origin, profile));
  PrefDeveloperProfileStore persisted(&prefs_, false);
  ASSERT_TRUE(persisted.Get(origin));
  ASSERT_EQ(1u, persisted.Get(origin)->assets.size());
  EXPECT_EQ("restart", persisted.Get(origin)->assets.front().id);

  ASSERT_TRUE(helper.GetProfile(origin));
  EXPECT_EQ(3u, helper.GetProfile(origin)->assets.size());
  std::vector<DeveloperAsset> first = helper.TakeAssetsForNavigation(url);
  EXPECT_EQ(3u, first.size());
  EXPECT_EQ(first, helper.active_assets());
  std::vector<DeveloperAsset> second = helper.TakeAssetsForNavigation(url);
  ASSERT_EQ(2u, second.size());
  EXPECT_EQ(second, helper.active_assets());
  EXPECT_EQ(second.end(), std::ranges::find(second, std::string("once"),
                                            &DeveloperAsset::id));
  ASSERT_TRUE(helper.GetProfile(origin));
  EXPECT_EQ(2u, helper.GetProfile(origin)->assets.size());
}

TEST_F(DeveloperProfileRuntimeTest,
       KeepsTransientOnlyProfileInEditorWithoutPersistentShell) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  const GURL url("https://transient-only.test/path");
  const url::Origin origin = url::Origin::Create(url);
  DeveloperProfile profile{.name = "Transient only"};
  profile.assets.push_back(Asset(
      "once", DeveloperAssetLifetime::kOnce,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = origin.Serialize()}));
  profile.assets.push_back(Asset("reload", DeveloperAssetLifetime::kReload,
                                 {.kind = DeveloperAssetScopeKind::kCurrentTab,
                                  .value = helper.tab_token()}));

  ASSERT_TRUE(helper.SaveProfile(origin, profile));
  PrefDeveloperProfileStore persisted(&prefs_, false);
  EXPECT_FALSE(persisted.Get(origin));
  const std::optional<DeveloperProfile> merged = helper.GetProfile(origin);
  ASSERT_TRUE(merged);
  EXPECT_EQ(profile.name, merged->name);
  ASSERT_EQ(2u, merged->assets.size());
  EXPECT_NE(merged->assets.end(),
            std::ranges::find(merged->assets, std::string("once"),
                              &DeveloperAsset::id));
  EXPECT_NE(merged->assets.end(),
            std::ranges::find(merged->assets, std::string("reload"),
                              &DeveloperAsset::id));

  helper.TakeAssetsForNavigation(url);
  const std::optional<DeveloperProfile> remaining = helper.GetProfile(origin);
  ASSERT_TRUE(remaining);
  ASSERT_EQ(1u, remaining->assets.size());
  EXPECT_EQ("reload", remaining->assets.front().id);
  EXPECT_FALSE(persisted.Get(origin));
}

TEST_F(DeveloperProfileRuntimeTest,
       RejectsCapacityDivergenceWithoutChangingAnyProfileSnapshot) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  PrefDeveloperProfileStore persisted(&prefs_, false);
  const url::Origin target =
      url::Origin::Create(GURL("https://external-0.test/"));

  for (size_t index = 0; index < kMaxDeveloperProfiles; ++index) {
    const GURL url =
        index == 0
            ? target.GetURL()
            : GURL("https://transient-" + std::to_string(index) + ".test/");
    const url::Origin origin = url::Origin::Create(url);
    DeveloperProfile profile{.name = "Transient capacity"};
    profile.assets.push_back(Asset("reload", DeveloperAssetLifetime::kReload,
                                   {.kind = DeveloperAssetScopeKind::kOrigin,
                                    .value = origin.Serialize()}));
    ASSERT_TRUE(helper.SaveProfile(origin, profile)) << index;
    ASSERT_FALSE(persisted.Get(origin)) << index;
  }
  for (size_t index = 0; index < kMaxDeveloperProfiles; ++index) {
    const GURL url("https://once-" + std::to_string(index) + ".test/");
    const url::Origin origin = url::Origin::Create(url);
    DeveloperProfile profile{.name = "Once capacity"};
    profile.assets.push_back(Asset("once", DeveloperAssetLifetime::kOnce,
                                   {.kind = DeveloperAssetScopeKind::kOrigin,
                                    .value = origin.Serialize()}));
    ASSERT_TRUE(helper.SaveProfile(origin, profile)) << index;
    ASSERT_FALSE(persisted.Get(origin)) << index;
  }

  DeveloperProfile old_profile{.name = "External profile"};
  old_profile.header_rules_enabled = true;
  old_profile.header_rules.push_back(
      {.name = "X-External-Secret",
       .secret_reference = "ahoi-keychain:old-secret",
       .action = DeveloperHeaderAction::kSet});
  for (size_t index = 0; index < kMaxDeveloperProfiles; ++index) {
    const GURL url("https://external-" + std::to_string(index) + ".test/");
    const url::Origin origin = url::Origin::Create(url);
    DeveloperProfile profile = old_profile;
    profile.name = "External " + std::to_string(index);
    ASSERT_TRUE(persisted.Set(origin, profile)) << index;
    if (origin == target) {
      old_profile = std::move(profile);
    }
  }
  ASSERT_EQ(kMaxDeveloperProfiles, persisted.ListOrigins().size());

  DeveloperProfile replacement{.name = "Replacement"};
  replacement.header_rules_enabled = true;
  replacement.header_rules.push_back(
      {.name = "X-External-Secret",
       .secret_reference = "ahoi-keychain:new-secret",
       .action = DeveloperHeaderAction::kSet});
  replacement.assets.push_back(Asset(
      "replacement-reload", DeveloperAssetLifetime::kReload,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = target.Serialize()}));
  replacement.assets.push_back(Asset(
      "replacement-once", DeveloperAssetLifetime::kOnce,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = target.Serialize()}));

  EXPECT_FALSE(helper.SaveProfile(target, replacement));
  ASSERT_TRUE(persisted.Get(target));
  EXPECT_EQ(old_profile, *persisted.Get(target));
  ASSERT_TRUE(helper.GetProfile(target));
  ASSERT_EQ(1u, helper.GetProfile(target)->assets.size());
  EXPECT_EQ("reload", helper.GetProfile(target)->assets.front().id);
  EXPECT_EQ(old_profile.header_rules, helper.GetProfile(target)->header_rules);
  EXPECT_TRUE(helper.active_assets().empty());
}

TEST_F(DeveloperProfileRuntimeTest,
       ActionExecutorChipsFollowLastCommittedEnabledAssetsOnly) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  std::unique_ptr<DeveloperActionExecutor> executor = CreateActionExecutor();
  const GURL url("https://chips.test/path");
  const url::Origin origin = url::Origin::Create(url);
  const DeveloperAssetScope current_tab_scope{
      .kind = DeveloperAssetScopeKind::kCurrentTab,
      .value = helper.tab_token()};

  DeveloperProfile initial{.name = "Initial chip"};
  initial.assets.push_back(
      Asset("script", DeveloperAssetLifetime::kReload, current_tab_scope));
  initial.assets.push_back(Asset("disabled-style",
                                 DeveloperAssetLifetime::kReload,
                                 current_tab_scope, DeveloperAssetKind::kStyle,
                                 /*enabled=*/false));
  initial.assets.push_back(Asset(
      "disabled-persistent", DeveloperAssetLifetime::kRestart,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = origin.Serialize()},
      DeveloperAssetKind::kStyle, /*enabled=*/false));
  ASSERT_TRUE(helper.SaveProfile(origin, initial));
  ASSERT_EQ(1u, helper.TakeAssetsForNavigation(url).size());

  DeveloperActivationState state =
      executor->GetActivationState(web_contents_.get());
  EXPECT_TRUE(state.Has(DeveloperActivation::kJavaScript));
  EXPECT_FALSE(state.Has(DeveloperActivation::kCss));

  DeveloperProfile edited{.name = "Edited chip"};
  edited.assets.push_back(Asset("style", DeveloperAssetLifetime::kReload,
                                current_tab_scope, DeveloperAssetKind::kStyle));
  edited.assets.push_back(Asset(
      "disabled-script", DeveloperAssetLifetime::kRestart,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = origin.Serialize()},
      DeveloperAssetKind::kJavaScript, /*enabled=*/false));
  ASSERT_TRUE(helper.SaveProfile(origin, edited));

  // Saving edits does not claim that the new assets are active before the
  // next document commit/navigation boundary.
  state = executor->GetActivationState(web_contents_.get());
  EXPECT_TRUE(state.Has(DeveloperActivation::kJavaScript));
  EXPECT_FALSE(state.Has(DeveloperActivation::kCss));

  ASSERT_EQ(1u, helper.TakeAssetsForNavigation(url).size());
  state = executor->GetActivationState(web_contents_.get());
  EXPECT_FALSE(state.Has(DeveloperActivation::kJavaScript));
  EXPECT_TRUE(state.Has(DeveloperActivation::kCss));
}

TEST_F(DeveloperProfileRuntimeTest,
       ResetRemovesAllContributingScopesButPreservesUnrelatedAssets) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  const GURL owner_url("https://example.test/owner");
  const url::Origin owner_origin = url::Origin::Create(owner_url);
  const GURL target_url("https://sub.example.test/docs/page");
  const url::Origin target_origin = url::Origin::Create(target_url);

  DeveloperProfile owner{.name = "Domain owner"};
  owner.assets.push_back(
      Asset("domain", DeveloperAssetLifetime::kRestart,
            {.kind = DeveloperAssetScopeKind::kDomain, .value = "example.test"},
            DeveloperAssetKind::kStyle));
  owner.assets.push_back(Asset("current-tab", DeveloperAssetLifetime::kReload,
                               {.kind = DeveloperAssetScopeKind::kCurrentTab,
                                .value = helper.tab_token()}));
  owner.assets.push_back(Asset("owner-only", DeveloperAssetLifetime::kRestart,
                               {.kind = DeveloperAssetScopeKind::kOrigin,
                                .value = owner_origin.Serialize()}));
  ASSERT_TRUE(helper.SaveProfile(owner_origin, owner));

  DeveloperProfile target{.name = "Exact target"};
  target.user_agent_enabled = true;
  target.user_agent = "Ahoi Reset Test";
  target.header_rules_enabled = true;
  target.header_rules.push_back(
      {.name = "X-Reset-Secret",
       .secret_reference = "ahoi-keychain:reset-secret",
       .action = DeveloperHeaderAction::kSet});
  target.assets.push_back(Asset("origin", DeveloperAssetLifetime::kRestart,
                                {.kind = DeveloperAssetScopeKind::kOrigin,
                                 .value = target_origin.Serialize()}));
  ASSERT_TRUE(helper.SaveProfile(target_origin, target));

  ASSERT_EQ(3u, helper.TakeAssetsForNavigation(target_url).size());
  ASSERT_FALSE(helper.active_assets().empty());
  ASSERT_TRUE(helper.ResetProfilesForUrl(target_url));
  EXPECT_TRUE(helper.active_assets().empty());
  EXPECT_TRUE(helper.TakeAssetsForNavigation(target_url).empty());

  PrefDeveloperProfileStore persisted(&prefs_, false);
  ASSERT_TRUE(persisted.Get(owner_origin));
  ASSERT_EQ(1u, persisted.Get(owner_origin)->assets.size());
  EXPECT_EQ("owner-only", persisted.Get(owner_origin)->assets.front().id);
  ASSERT_TRUE(persisted.Get(target_origin));
  EXPECT_TRUE(persisted.Get(target_origin)->assets.empty());
  EXPECT_FALSE(persisted.Get(target_origin)->user_agent_enabled);
  EXPECT_EQ("Ahoi Reset Test", persisted.Get(target_origin)->user_agent);
  EXPECT_FALSE(persisted.Get(target_origin)->header_rules_enabled);
  ASSERT_EQ(1u, persisted.Get(target_origin)->header_rules.size());
  EXPECT_EQ(
      "ahoi-keychain:reset-secret",
      persisted.Get(target_origin)->header_rules.front().secret_reference);
  ASSERT_TRUE(helper.GetProfile(owner_origin));
  ASSERT_EQ(1u, helper.GetProfile(owner_origin)->assets.size());
  ASSERT_TRUE(helper.GetProfile(target_origin));
  EXPECT_TRUE(helper.GetProfile(target_origin)->assets.empty());
}

TEST_F(DeveloperProfileRuntimeTest,
       DisabledTransientAndPersistentAssetsNeverBecomeActive) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  std::unique_ptr<DeveloperActionExecutor> executor = CreateActionExecutor();
  const GURL url("https://disabled.test/");
  const url::Origin origin = url::Origin::Create(url);
  DeveloperProfile profile{.name = "Disabled"};
  profile.assets.push_back(Asset("transient", DeveloperAssetLifetime::kReload,
                                 {.kind = DeveloperAssetScopeKind::kCurrentTab,
                                  .value = helper.tab_token()},
                                 DeveloperAssetKind::kJavaScript,
                                 /*enabled=*/false));
  profile.assets.push_back(Asset(
      "persistent", DeveloperAssetLifetime::kRestart,
      {.kind = DeveloperAssetScopeKind::kOrigin, .value = origin.Serialize()},
      DeveloperAssetKind::kStyle, /*enabled=*/false));

  ASSERT_TRUE(helper.SaveProfile(origin, profile));
  EXPECT_TRUE(helper.TakeAssetsForNavigation(url).empty());
  EXPECT_TRUE(helper.active_assets().empty());
  const DeveloperActivationState state =
      executor->GetActivationState(web_contents_.get());
  EXPECT_FALSE(state.Has(DeveloperActivation::kJavaScript));
  EXPECT_FALSE(state.Has(DeveloperActivation::kCss));
}

TEST_F(DeveloperProfileRuntimeTest, RejectsAnotherTabsCurrentTabToken) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  const GURL url("https://example.test/");
  const url::Origin origin = url::Origin::Create(url);
  DeveloperProfile profile{.name = "Wrong tab"};
  profile.assets.push_back(Asset("wrong-tab", DeveloperAssetLifetime::kReload,
                                 {.kind = DeveloperAssetScopeKind::kCurrentTab,
                                  .value = "another-tab-token"}));
  EXPECT_FALSE(helper.SaveProfile(origin, profile));
}

TEST_F(DeveloperProfileRuntimeTest, MovesLookupMarkerAcrossDiscardReplacement) {
  DeveloperProfileTabHelper helper(web_contents_.get(), &prefs_);
  auto replacement = content::WebContentsTester::CreateTestWebContents(
      &browser_context_, nullptr);
  helper.SetWebContents(replacement.get());
  EXPECT_EQ(nullptr,
            DeveloperProfileTabHelper::FromWebContents(web_contents_.get()));
  EXPECT_EQ(&helper,
            DeveloperProfileTabHelper::FromWebContents(replacement.get()));
}

TEST_F(DeveloperProfileRuntimeTest, LookupMarkerExpiresWithHelperLifetime) {
  auto helper =
      std::make_unique<DeveloperProfileTabHelper>(web_contents_.get(), &prefs_);
  EXPECT_EQ(helper.get(),
            DeveloperProfileTabHelper::FromWebContents(web_contents_.get()));
  helper.reset();
  EXPECT_EQ(nullptr,
            DeveloperProfileTabHelper::FromWebContents(web_contents_.get()));
}

TEST_F(DeveloperProfileRuntimeTest,
       WebContentsCanBeDestroyedBeforeOwnedHelper) {
  auto helper =
      std::make_unique<DeveloperProfileTabHelper>(web_contents_.get(), &prefs_);
  web_contents_.reset();
  helper.reset();
}

}  // namespace
}  // namespace ahoi
