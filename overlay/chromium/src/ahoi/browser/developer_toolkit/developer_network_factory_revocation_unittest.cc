// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_test_support.h"

namespace ahoi::test {

TEST_F(DeveloperNetworkFactoryProxyTest,
       MasterDisableAfterDispatchLeavesResponseHeadersNative) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->loaders.size());
  EXPECT_EQ("configured", terminal_->seen[0].headers.GetHeader("X-Ahoi-Dev"));
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
  EXPECT_EQ("kept", client.head->headers->GetNormalizedHeader("X-Native"));
  terminal_->loaders[0]->Complete();
  environment_.RunUntilIdle();
  EXPECT_EQ(net::OK, client.completed);
  EXPECT_EQ(1, client.completion_count);
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       TemporaryResetAfterDispatchRestoresRedirectWithoutExemptResurrection) {
  profile_.headers_persistent = false;
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  auto request = Request();
  request.headers.SetHeader("X-Ahoi-Dev", "original");
  Load(*factory, client, request);
  ASSERT_EQ(1u, terminal_->loaders.size());
  ASSERT_EQ("request-1", terminal_->seen[0].headers.GetHeader("X-Ahoi-Dev"));
  ASSERT_TRUE(helper_->ResetProfilesForUrl(origin_.GetURL()));
  auto* native = terminal_->loaders[0].get();
  native->Redirect(origin_.GetURL().Resolve("same-origin-next"));
  environment_.RunUntilIdle();
  network::HttpRequestHeadersUpdateParams pending;
  pending.modified_headers.SetHeader("X-Ahoi-Dev", "late");
  pending.modified_cors_exempt_headers.SetHeader("X-Ahoi-Dev", "late-exempt");
  loader_->FollowRedirect(std::move(pending), std::nullopt);
  environment_.RunUntilIdle();
  EXPECT_TRUE(native->followed);
  EXPECT_EQ("original", native->headers.GetHeader("X-Ahoi-Dev"));
  EXPECT_FALSE(native->exempt_headers.HasHeader("X-Ahoi-Dev"));
  native->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       ReenableAfterDispatchCannotRevivePriorResponseApproval) {
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->loaders.size());
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
  Save();
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       ReplacedTemporaryOwnerAfterDispatchCannotApplyResponseRules) {
  profile_.headers_persistent = false;
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->loaders.size());
  helper_.reset();
  helper_ = std::make_unique<DeveloperProfileTabHelper>(contents_.get(), &prefs_);
  Save();
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}


TEST_F(DeveloperNetworkFactoryProxyTest,
       ResetPersistentRulesStopsNewPlainRequestsOnExistingFactory) {
  auto factory = Build();
  ASSERT_TRUE(helper_->ResetProfilesForUrl(origin_.GetURL()));
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

}  // namespace ahoi::test
