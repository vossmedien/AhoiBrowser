// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_test_support.h"

namespace ahoi::test {

TEST_F(DeveloperNetworkFactoryProxyTest,
       MainFrameNavigationKeepsNativeFactoryDefaults) {
  auto factory = Build();
  ClientSink client;
  auto request = Request();
  request.mode = network::mojom::RequestMode::kNavigate;
  request.destination = network::mojom::RequestDestination::kDocument;
  Load(*factory, client, request);
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       SubframeNavigationCannotBorrowParentProfile) {
  auto request = Request();
  request.is_outermost_main_frame = false;
  request.mode = network::mojom::RequestMode::kNavigate;
  request.navigation_redirect_chain.push_back(request.url);
  EXPECT_FALSE(GetDeveloperProfileNetworkSnapshotForRequest(
      request, &prefs_, false, contents_.get()));
}

TEST_F(DeveloperNetworkFactoryProxyTest, RulesAndCacheReachLaterInterceptor) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_DISABLE_CACHE);
  EXPECT_EQ("configured", interceptor_->seen[0].headers.GetHeader("X-Ahoi-Dev"));
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_EQ("present", client.head->headers->GetNormalizedHeader(
                           "X-Ahoi-Response"));
  EXPECT_EQ("kept", client.head->headers->GetNormalizedHeader("X-Native"));
  terminal_->loaders[0]->Complete();
  environment_.RunUntilIdle();
  EXPECT_EQ(net::OK, client.completed);
  EXPECT_EQ(1, client.completion_count);
}

TEST_F(DeveloperNetworkFactoryProxyTest, CrossOriginRedirectCannotResurrectRule) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  auto* native = terminal_->loaders[0].get();
  native->Redirect(GURL("https://other.test/after"));
  environment_.RunUntilIdle();
  network::HttpRequestHeadersUpdateParams renderer_changes;
  renderer_changes.modified_headers.SetHeader("X-Ahoi-Dev", "late-value");
  renderer_changes.modified_cors_exempt_headers.SetHeader("X-Ahoi-Dev", "late");
  loader_->FollowRedirect(std::move(renderer_changes), std::nullopt);
  environment_.RunUntilIdle();
  EXPECT_TRUE(native->followed);
  EXPECT_FALSE(native->headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_FALSE(native->exempt_headers.HasHeader("X-Ahoi-Dev"));
  native->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       CrossOriginResourcesBypassCacheWithoutOriginHeaders) {
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  auto request = Request();
  request.url = GURL("https://cdn.test/style.css");
  Load(*factory, client, request);
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_DISABLE_CACHE);
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, secrets_->request_reads);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       CrossOriginResourcesWithoutCacheOptInKeepNativeDefaults) {
  profile_.cache_disabled = false;
  Save();
  auto factory = Build();
  ClientSink client;
  auto request = Request();
  request.url = GURL("https://cdn.test/style.css");
  Load(*factory, client, request);
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
}

TEST_F(DeveloperNetworkFactoryProxyTest, SameOriginRedirectForwardsControl) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  auto* native = terminal_->loaders[0].get();
  native->Redirect(origin_.GetURL().Resolve("redirect"));
  environment_.RunUntilIdle();
  const GURL alternate = origin_.GetURL().Resolve("alternate");
  loader_->FollowRedirect(network::HttpRequestHeadersUpdateParams(), alternate);
  loader_->SetPriority(net::HIGHEST, 42);
  environment_.RunUntilIdle();
  EXPECT_EQ(alternate, native->override_url);
  EXPECT_EQ(net::HIGHEST, native->priority);
  EXPECT_EQ(42, native->intra_priority);
  EXPECT_EQ("configured", native->headers.GetHeader("X-Ahoi-Dev"));
  native->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_TRUE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       RedirectRestoresOriginalValueWithoutExemptOverride) {
  auto factory = Build();
  ClientSink client;
  auto request = Request();
  request.headers.SetHeader("X-Ahoi-Dev", "original");
  Load(*factory, client, request);
  auto* native = terminal_->loaders[0].get();
  native->Redirect(GURL("https://other.test/after"));
  environment_.RunUntilIdle();
  network::HttpRequestHeadersUpdateParams renderer_changes;
  renderer_changes.modified_headers.SetHeader("X-Ahoi-Dev", "late");
  renderer_changes.modified_cors_exempt_headers.SetHeader("X-Ahoi-Dev", "late");
  loader_->FollowRedirect(std::move(renderer_changes), std::nullopt);
  environment_.RunUntilIdle();
  EXPECT_TRUE(native->followed);
  EXPECT_EQ("original", native->headers.GetHeader("X-Ahoi-Dev"));
  EXPECT_FALSE(native->exempt_headers.HasHeader("X-Ahoi-Dev"));
}

TEST_F(DeveloperNetworkFactoryProxyTest, ExtensionInterceptorCanStillBlock) {
  auto factory = Build();
  interceptor_->block = true;
  ClientSink client;
  Load(*factory, client, Request());
  EXPECT_TRUE(terminal_->seen.empty());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client.completed);
  EXPECT_EQ(1, client.completion_count);
}

TEST_F(DeveloperNetworkFactoryProxyTest, DisabledAndUnknownFactoryAddNoHop) {
  auto unknown = Build(false, std::nullopt, false);
  EXPECT_EQ(terminal_.get(), unknown.get());
  unknown.reset();
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  auto disabled = Build(true, std::nullopt, false);
  EXPECT_EQ(terminal_.get(), disabled.get());
  ClientSink client;
  Load(*disabled, client, Request());
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
}

TEST_F(DeveloperNetworkFactoryProxyTest, MasterDisableStopsExistingFactory) {
  auto factory = Build();
  prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
  ClientSink client;
  Load(*factory, client, Request());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
}

TEST_F(DeveloperNetworkFactoryProxyTest, NavigatedAndClosedTabDoNotReuseState) {
  auto factory = Build();
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      contents_.get(), GURL("https://other.test/"));
  ClientSink navigated;
  Load(*factory, navigated, Request());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  loader_.reset();
  contents_.reset();
  ClientSink closed;
  Load(*factory, closed, Request());
  EXPECT_FALSE(terminal_->seen[1].headers.HasHeader("X-Ahoi-Dev"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       SameOriginNewDocumentCannotUseOldFactoryApproval) {
  auto factory = Build();
  const int64_t old_id = contents_->GetPrimaryMainFrame()->GetNavigationId();
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      contents_.get(), origin_.GetURL().Resolve("next-document"));
  ASSERT_NE(old_id, contents_->GetPrimaryMainFrame()->GetNavigationId());
  Save();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       PendingFactoryCannotUsePreviousCommittedDocument) {
  auto factory = Build(
      true, contents_->GetPrimaryMainFrame()->GetNavigationId() + 1);
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
}

TEST_F(DeveloperNetworkFactoryProxyTest, UnresolvedSecretsDisableBothDirections) {
  profile_.header_rules[0].value.clear();
  profile_.header_rules[0].secret_reference = "ahoi-keychain:synthetic";
  Save();
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest, RendererDisconnectCancelsNativeLoader) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  client.Disconnect();
  environment_.RunUntilIdle();
  EXPECT_TRUE(terminal_->loaders[0]->disconnected);
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       SecretsAreResolvedForEachRequestWithoutDocumentPlaintext) {
  UseSecrets();
  auto factory = Build();
  for (int index = 0; index < 2; ++index) {
    ClientSink client;
    Load(*factory, client, Request());
    ASSERT_EQ(static_cast<size_t>(index + 1), terminal_->seen.size());
    EXPECT_EQ("request-" + std::to_string(index + 1),
              terminal_->seen[index].headers.GetHeader("X-Ahoi-Dev"));
    terminal_->loaders[index]->Respond();
    environment_.RunUntilIdle();
    ASSERT_TRUE(client.head);
    EXPECT_EQ("response-value", client.head->headers->GetNormalizedHeader(
                                    "X-Ahoi-Response"));
    auto metadata = GetDeveloperProfileNetworkSnapshotForRequest(
        Request(), &prefs_, false, contents_.get());
    ASSERT_TRUE(metadata);
    EXPECT_TRUE(metadata->header_rules[0].value.empty());
    EXPECT_FALSE(metadata->header_rules[0].secret_reference.empty());
    terminal_->loaders[index]->Complete();
    environment_.RunUntilIdle();
    loader_.reset();
  }
  EXPECT_EQ(2, secrets_->request_reads);
}

TEST_F(DeveloperNetworkFactoryProxyTest, FailedSecretResolutionIsAtomic) {
  UseSecrets();
  secrets_->resolve_response = false;
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_NE(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       DeferredSecretsRecheckEnablementRulesAndDocument) {
  for (int revoke = 0; revoke < 4; ++revoke) {
    SCOPED_TRACE(revoke);
    prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
    UseSecrets();
    auto factory = Build();
    ClientSink client;
    QueueLoad(*factory, client);
    if (revoke == 0) {
      prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
    } else if (revoke == 1) {
      PrefDeveloperProfileStore store(&prefs_, false);
      ASSERT_TRUE(store.Remove(origin_));
    } else if (revoke == 2) {
      content::NavigationSimulator::NavigateAndCommitFromBrowser(
          contents_.get(), origin_.GetURL().Resolve("replacement"));
    } else {
      prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, false);
      prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
      Save();
    }
    secrets_->allow_read.Signal();
    environment_.RunUntilIdle();
    ASSERT_EQ(1u, terminal_->seen.size());
    EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
    EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
    loader_.reset();
    factory.reset();
    environment_.RunUntilIdle();
  }
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       DeferredSecretsCancelBeforeNativeLoadOnRendererDisconnect) {
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  QueueLoad(*factory, client);
  client.Disconnect();
  base::RunLoop().RunUntilIdle();
  secrets_->allow_read.Signal();
  environment_.RunUntilIdle();
  EXPECT_TRUE(terminal_->seen.empty());
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       DeferredSecretRequestPreservesEarlyPriority) {
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  QueueLoad(*factory, client);
  loader_->SetPriority(net::HIGHEST, 99);
  base::RunLoop().RunUntilIdle();
  secrets_->allow_read.Signal();
  environment_.RunUntilIdle();
  ASSERT_EQ(1u, terminal_->loaders.size());
  EXPECT_EQ(net::HIGHEST, terminal_->loaders[0]->priority);
  EXPECT_EQ(99, terminal_->loaders[0]->intra_priority);
}

TEST_F(DeveloperNetworkFactoryProxyTest, NativeDisconnectCompletesExactlyOnce) {
  auto factory = Build();
  ClientSink client;
  Load(*factory, client, Request());
  terminal_->loaders[0]->DisconnectClient();
  environment_.RunUntilIdle();
  EXPECT_EQ(net::ERR_ABORTED, client.completed);
  EXPECT_EQ(1, client.completion_count);
}


TEST_F(DeveloperNetworkFactoryProxyTest,
       TemporarySecretsUseRealFactoryAndStayReferencesBetweenRequests) {
  profile_.headers_persistent = false;
  UseSecrets();
  auto factory = Build();
  for (int index = 0; index < 2; ++index) {
    ClientSink client;
    Load(*factory, client, Request());
    ASSERT_EQ(static_cast<size_t>(index + 1), terminal_->seen.size());
    EXPECT_EQ("request-" + std::to_string(index + 1),
              terminal_->seen[index].headers.GetHeader("X-Ahoi-Dev"));
    terminal_->loaders[index]->Respond();
    environment_.RunUntilIdle();
    ASSERT_TRUE(client.head);
    EXPECT_EQ("response-value", client.head->headers->GetNormalizedHeader(
                                    "X-Ahoi-Response"));
    const auto metadata = helper_->GetProfile(origin_);
    ASSERT_TRUE(metadata);
    EXPECT_FALSE(metadata->headers_persistent);
    EXPECT_TRUE(metadata->header_rules[0].value.empty());
    EXPECT_EQ(helper_->tab_token(), metadata->headers_owner_token);
    terminal_->loaders[index]->Complete();
    environment_.RunUntilIdle();
    loader_.reset();
  }
  PrefDeveloperProfileStore durable(&prefs_, false);
  ASSERT_TRUE(durable.Get(origin_));
  EXPECT_TRUE(durable.Get(origin_)->header_rules.empty());
  EXPECT_TRUE(durable.Get(origin_)->response_header_rules.empty());
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       ResetTemporaryHeadersRejectsDeferredSecretReplyInBothDirections) {
  profile_.headers_persistent = false;
  UseSecrets();
  auto factory = Build();
  ClientSink client;
  QueueLoad(*factory, client);
  ASSERT_TRUE(helper_->ResetProfilesForUrl(origin_.GetURL()));
  secrets_->allow_read.Signal();
  environment_.RunUntilIdle();
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       ReplacedTemporaryOwnerRejectsDeferredReplyEvenWithIdenticalRules) {
  profile_.headers_persistent = false;
  UseSecrets();
  const auto original_owner = helper_->tab_token();
  auto factory = Build();
  ClientSink client;
  QueueLoad(*factory, client);
  helper_.reset();
  helper_ = std::make_unique<DeveloperProfileTabHelper>(contents_.get(), &prefs_);
  ASSERT_NE(original_owner, helper_->tab_token());
  Save();
  secrets_->allow_read.Signal();
  environment_.RunUntilIdle();
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, terminal_->seen[0].load_flags & net::LOAD_BYPASS_CACHE);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

TEST_F(DeveloperNetworkFactoryProxyTest,
       RetiredTemporaryOwnerStopsAlreadyBuiltFactoryWithoutSecretResolution) {
  profile_.headers_persistent = false;
  UseSecrets();
  auto factory = Build();
  helper_.reset();
  ClientSink client;
  Load(*factory, client, Request());
  ASSERT_EQ(1u, terminal_->seen.size());
  EXPECT_FALSE(terminal_->seen[0].headers.HasHeader("X-Ahoi-Dev"));
  EXPECT_EQ(0, secrets_->request_reads);
  terminal_->loaders[0]->Respond();
  environment_.RunUntilIdle();
  ASSERT_TRUE(client.head);
  EXPECT_FALSE(client.head->headers->HasHeader("X-Ahoi-Response"));
}

}  // namespace ahoi::test
