// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_proxy.h"

#include <memory>
#include <atomic>
#include <optional>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

network::mojom::URLResponseHeadPtr Response(const char* status = "200 OK") {
  auto head = network::mojom::URLResponseHead::New();
  head->headers = net::HttpResponseHeaders::Builder({1, 1}, status)
                      .AddHeader("X-Native", "kept")
                      .Build();
  return head;
}

struct SecretFixture {
  std::atomic<int> request_reads = 0;
  bool resolve_response = true;
  base::WaitableEvent entered;
  base::WaitableEvent allow_read{base::WaitableEvent::ResetPolicy::MANUAL,
                                base::WaitableEvent::InitialState::SIGNALED};
};

class FixtureSecretStore final : public DeveloperSecretStore {
 public:
  explicit FixtureSecretStore(std::shared_ptr<SecretFixture> fixture)
      : fixture_(std::move(fixture)) {}
  std::optional<std::string> Store(std::string_view,
                                   std::string_view) override {
    return std::nullopt;
  }
  bool Remove(std::string_view) override { return false; }
  std::optional<std::string> Resolve(std::string_view reference) const override {
    if (reference == "ahoi-keychain:test-request") {
      fixture_->entered.Signal();
      base::ScopedAllowBaseSyncPrimitivesForTesting allow_test_gate;
      fixture_->allow_read.Wait();
      return "request-" + std::to_string(++fixture_->request_reads);
    }
    if (reference == "ahoi-keychain:test-response" &&
        fixture_->resolve_response) {
      return "response-value";
    }
    return std::nullopt;
  }

 private:
  const std::shared_ptr<SecretFixture> fixture_;
};

class ClientSink final : public network::mojom::URLLoaderClient {
 public:
  mojo::PendingRemote<network::mojom::URLLoaderClient> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  void Disconnect() { receiver_.reset(); }
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr) override {}
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr response,
                         mojo::ScopedDataPipeConsumerHandle,
                         std::optional<mojo_base::BigBuffer>) override {
    head = std::move(response);
  }
  void OnReceiveRedirect(const net::RedirectInfo& redirect,
                         network::mojom::URLResponseHeadPtr) override {
    redirect_url = redirect.new_url;
  }
  void OnUploadProgress(int64_t, int64_t,
                        OnUploadProgressCallback callback) override {
    std::move(callback).Run();
  }
  void OnTransferSizeUpdated(int32_t) override {}
  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    completed = status.error_code;
    ++completion_count;
  }
  network::mojom::URLResponseHeadPtr head;
  GURL redirect_url;
  std::optional<int> completed;
  int completion_count = 0;

 private:
  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
};

class NativeLoader final : public network::mojom::URLLoader {
 public:
  NativeLoader(mojo::PendingReceiver<network::mojom::URLLoader> receiver,
                mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                const network::ResourceRequest& request)
      : headers(request.headers), exempt_headers(request.cors_exempt_headers),
        receiver_(this, std::move(receiver)), client_(std::move(client)) {
    receiver_.set_disconnect_handler(base::BindOnce(
        &NativeLoader::OnDisconnect, base::Unretained(this)));
  }
  void FollowRedirect(network::HttpRequestHeadersUpdateParams changes,
                       const std::optional<GURL>& url) override {
    changes.Apply(headers, exempt_headers);
    override_url = url;
    followed = true;
  }
  void SetPriority(net::RequestPriority value, int32_t intra) override {
    priority = value;
    intra_priority = intra;
  }
  void Redirect(const GURL& url) {
    net::RedirectInfo info;
    info.new_url = url;
    info.status_code = 302;
    info.new_method = "GET";
    client_->OnReceiveRedirect(info, Response("302 Found"));
  }
  void Respond() {
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    ASSERT_EQ(MOJO_RESULT_OK, mojo::CreateDataPipe(nullptr, producer, consumer));
    producer.reset();
    client_->OnReceiveResponse(Response(), std::move(consumer), std::nullopt);
  }
  void Complete() {
    client_->OnComplete(network::URLLoaderCompletionStatus(net::OK));
  }
  void DisconnectClient() { client_.reset(); }
  net::HttpRequestHeaders headers;
  net::HttpRequestHeaders exempt_headers;
  std::optional<GURL> override_url;
  bool followed = false;
  bool disconnected = false;
  net::RequestPriority priority = net::IDLE;
  int32_t intra_priority = -1;

 private:
  void OnDisconnect() { disconnected = true; }
  mojo::Receiver<network::mojom::URLLoader> receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
};

// This is a later extension interceptor or the terminal native factory.
// Its IPC contract exercises the production adapter, not a second algorithm.
class RecordingFactory final : public network::SharedURLLoaderFactory {
 public:
  explicit RecordingFactory(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> next =
          mojo::NullRemote()) : next_(std::move(next)) {}
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t id, uint32_t options, const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override {
    seen.push_back(request);
    if (block) {
      mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
          ->OnComplete(
              network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    } else if (next_) {
      next_->CreateLoaderAndStart(std::move(receiver), id, options, request,
                                  std::move(client), annotation);
    } else {
      loaders.push_back(std::make_unique<NativeLoader>(
          std::move(receiver), std::move(client), request));
    }
  }
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> r) override {
    receivers_.Add(this, std::move(r));
  }
  std::unique_ptr<network::PendingSharedURLLoaderFactory> Clone() override {
    NOTREACHED();
  }
  std::vector<network::ResourceRequest> seen;
  std::vector<std::unique_ptr<NativeLoader>> loaders;
  bool block = false;

 private:
  ~RecordingFactory() override = default;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> next_;
};

class DeveloperNetworkFactoryProxyTest : public testing::Test {
 protected:
  void SetUp() override {
    // Toolkit registration also owns the profile dictionary.
    developer_toolkit_prefs::RegisterProfilePrefs(prefs_.registry());
    prefs_.SetBoolean(developer_toolkit_prefs::kToolkitEnabled, true);
    user_prefs::UserPrefs::Set(&context_, &prefs_);
    contents_ = content::WebContentsTester::CreateTestWebContents(
        &context_, nullptr);
    content::NavigationSimulator::NavigateAndCommitFromBrowser(
        contents_.get(), origin_.GetURL());
    helper_ = std::make_unique<DeveloperProfileTabHelper>(contents_.get(), &prefs_);
    profile_.name = "Synthetic network fixture";
    profile_.cache_disabled = true;
    profile_.header_rules_enabled = true;
    profile_.header_rules.push_back({.name = "X-Ahoi-Dev",
                                     .value = "configured",
                                     .action = DeveloperHeaderAction::kSet});
    profile_.response_header_rules_enabled = true;
    profile_.response_header_rules.push_back(
        {.name = "X-Ahoi-Response", .value = "present",
         .action = DeveloperHeaderAction::kSet});
    Save();
  }
  void TearDown() override {
    // Only the injected secret store is gated. Chromium's own cleanup worker
    // sequences must remain asynchronous, including after failed assertions.
    secrets_->allow_read.Signal();
    loader_.reset();
    interceptor_.reset();
    terminal_.reset();
    helper_.reset();
    contents_.reset();
    environment_.RunUntilIdle();
  }
  void Save() {
    ASSERT_TRUE(helper_->SaveProfile(origin_, profile_));
    UpdateDeveloperProfileNetworkState(*contents_, origin_.GetURL(),
                                       helper_->GetProfile(origin_));
  }
  scoped_refptr<network::SharedURLLoaderFactory> Build(
      bool document = true,
      std::optional<int64_t> navigation_id = std::nullopt,
      bool later_interceptor = true) {
    network::URLLoaderFactoryBuilder builder;
    MaybeProxyDeveloperProfileURLLoaderFactory(
        &prefs_, false, document, contents_->GetPrimaryMainFrame(),
        navigation_id, origin_, builder, base::BindRepeating(
            [](std::shared_ptr<SecretFixture> fixture) {
              return std::unique_ptr<DeveloperSecretStore>(
                  std::make_unique<FixtureSecretStore>(std::move(fixture)));
            }, secrets_));
    if (later_interceptor) {
      auto [receiver, remote] = builder.Append();
      interceptor_ = base::MakeRefCounted<RecordingFactory>(std::move(remote));
      interceptor_->Clone(std::move(receiver));
    }
    terminal_ = base::MakeRefCounted<RecordingFactory>();
    return std::move(builder).Finish(terminal_);
  }
  network::ResourceRequest Request() {
    network::ResourceRequest request;
    request.url = origin_.GetURL().Resolve("resource.css");
    request.request_initiator = origin_;
    return request;
  }
  void Load(network::SharedURLLoaderFactory& factory, ClientSink& client,
            const network::ResourceRequest& request) {
    factory.CreateLoaderAndStart(loader_.BindNewPipeAndPassReceiver(), 7, 0,
                                 request, client.Bind(),
                                 net::MutableNetworkTrafficAnnotationTag());
    environment_.RunUntilIdle();
  }
  void UseSecrets() {
    profile_.header_rules[0].value.clear();
    profile_.header_rules[0].secret_reference = "ahoi-keychain:test-request";
    profile_.response_header_rules[0].value.clear();
    profile_.response_header_rules[0].secret_reference =
        "ahoi-keychain:test-response";
    Save();
  }
  void QueueLoad(network::SharedURLLoaderFactory& factory, ClientSink& client) {
    secrets_->entered.Reset();
    secrets_->allow_read.Reset();
    factory.CreateLoaderAndStart(loader_.BindNewPipeAndPassReceiver(), 7, 0,
                                 Request(), client.Bind(),
                                 net::MutableNetworkTrafficAnnotationTag());
    base::RunLoop().RunUntilIdle();
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    EXPECT_TRUE(secrets_->entered.TimedWait(base::Seconds(3)));
    EXPECT_TRUE(terminal_->seen.empty());
  }
  content::BrowserTaskEnvironment environment_;
  content::RenderViewHostTestEnabler enabler_;
  TestingPrefServiceSimple prefs_;
  content::TestBrowserContext context_;
  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<DeveloperProfileTabHelper> helper_;
  const url::Origin origin_ = url::Origin::Create(GURL("https://site.test/"));
  DeveloperProfile profile_;
  scoped_refptr<RecordingFactory> interceptor_;
  scoped_refptr<RecordingFactory> terminal_;
  mojo::Remote<network::mojom::URLLoader> loader_;
  std::shared_ptr<SecretFixture> secrets_ = std::make_shared<SecretFixture>();
};

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

}  // namespace
}  // namespace ahoi
