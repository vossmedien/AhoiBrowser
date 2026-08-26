// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_asset_policy_view.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"

namespace ahoi {
namespace {

class DeveloperAssetPolicyViewTest : public views::ViewsTestBase {};

DeveloperAsset Asset() {
  return {.id = "asset",
          .name = "Asset",
          .enabled = true,
          .source = "body { color: red; }",
          .scope = {.kind = DeveloperAssetScopeKind::kOrigin,
                    .value = "https://app.example.com"},
          .lifetime = DeveloperAssetLifetime::kRestart,
          .sync_enabled = true};
}

TEST_F(DeveloperAssetPolicyViewTest, ResolvesEveryExplicitScope) {
  auto view = std::make_unique<DeveloperAssetPolicyView>(
      Asset(), GURL("https://app.example.com/docs/page"), "tab-token-1");
  DeveloperAsset result = Asset();

  view->SelectScopeForTesting(DeveloperAssetScopeKind::kCurrentTab);
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_EQ(DeveloperAssetScopeKind::kCurrentTab, result.scope.kind);
  EXPECT_EQ("tab-token-1", result.scope.value);
  EXPECT_EQ(DeveloperAssetLifetime::kReload, result.lifetime);
  EXPECT_FALSE(view->sync_control_enabled_for_testing());

  view->SelectScopeForTesting(DeveloperAssetScopeKind::kOrigin);
  view->SelectLifetimeForTesting(DeveloperAssetLifetime::kRestart);
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_EQ("https://app.example.com", result.scope.value);

  view->SelectScopeForTesting(DeveloperAssetScopeKind::kDomain);
  EXPECT_TRUE(view->domain_scope_warning_visible_for_testing());
  EXPECT_FALSE(view->ApplyTo(&result));
  view->SetDomainScopeWarningAcceptedForTesting(true);
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_EQ("example.com", result.scope.value);
  EXPECT_TRUE(result.domain_scope_warning_accepted);

  view->SelectScopeForTesting(DeveloperAssetScopeKind::kPath);
  EXPECT_FALSE(view->domain_scope_warning_visible_for_testing());
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_EQ("/docs/page", result.scope.value);
  EXPECT_FALSE(result.domain_scope_warning_accepted);
}

TEST_F(DeveloperAssetPolicyViewTest,
       RestoresOnlyAnExplicitLocalDomainAcknowledgement) {
  DeveloperAsset acknowledged = Asset();
  acknowledged.scope = {.kind = DeveloperAssetScopeKind::kDomain,
                        .value = "example.com"};
  acknowledged.domain_scope_warning_accepted = true;
  auto view = std::make_unique<DeveloperAssetPolicyView>(
      acknowledged, GURL("https://app.example.com/"), "tab-token-1");

  DeveloperAsset result = acknowledged;
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_TRUE(result.domain_scope_warning_accepted);

  view->SelectScopeForTesting(DeveloperAssetScopeKind::kOrigin);
  view->SelectScopeForTesting(DeveloperAssetScopeKind::kDomain);
  EXPECT_FALSE(view->ApplyTo(&result));
}

TEST_F(DeveloperAssetPolicyViewTest, SyncIsAvailableOnlyAcrossRestarts) {
  auto view = std::make_unique<DeveloperAssetPolicyView>(
      Asset(), GURL("https://app.example.com/"), "tab-token-1");
  EXPECT_TRUE(view->sync_control_enabled_for_testing());
  view->SetSyncEnabledForTesting(true);

  view->SelectLifetimeForTesting(DeveloperAssetLifetime::kOnce);
  EXPECT_FALSE(view->sync_control_enabled_for_testing());
  DeveloperAsset result = Asset();
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_EQ(DeveloperAssetLifetime::kOnce, result.lifetime);
  EXPECT_FALSE(result.sync_enabled);

  view->SelectLifetimeForTesting(DeveloperAssetLifetime::kRestart);
  EXPECT_TRUE(view->sync_control_enabled_for_testing());
  view->SetSyncEnabledForTesting(true);
  ASSERT_TRUE(view->ApplyTo(&result));
  EXPECT_TRUE(result.sync_enabled);
}

TEST_F(DeveloperAssetPolicyViewTest, RejectsCurrentTabWithoutOwnedToken) {
  auto view = std::make_unique<DeveloperAssetPolicyView>(
      Asset(), GURL("https://app.example.com/"), std::string());
  view->SelectScopeForTesting(DeveloperAssetScopeKind::kCurrentTab);
  DeveloperAsset result = Asset();
  EXPECT_FALSE(view->ApplyTo(&result));
}

}  // namespace
}  // namespace ahoi
