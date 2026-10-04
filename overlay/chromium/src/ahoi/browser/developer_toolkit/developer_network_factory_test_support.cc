// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_test_support.h"

namespace ahoi::test {

network::mojom::URLResponseHeadPtr Response(const char* status) {
  auto head = network::mojom::URLResponseHead::New();
  head->headers = net::HttpResponseHeaders::Builder({1, 1}, status)
                      .AddHeader("X-Native", "kept")
                      .Build();
  return head;
}

FixtureSecretStore::FixtureSecretStore(std::shared_ptr<SecretFixture> fixture)
      : fixture_(std::move(fixture)) {}

std::optional<std::string> FixtureSecretStore::Store(std::string_view,
                                   std::string_view) {
    return std::nullopt;
  }

bool FixtureSecretStore::Remove(std::string_view) { return false; }

std::optional<std::string> FixtureSecretStore::Resolve(std::string_view reference) const {
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

FixtureSecretStore::~FixtureSecretStore() = default;

mojo::PendingRemote<network::mojom::URLLoaderClient> ClientSink::Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

void ClientSink::Disconnect() { receiver_.reset(); }

void ClientSink::OnReceiveEarlyHints(network::mojom::EarlyHintsPtr) {}

void ClientSink::OnReceiveResponse(network::mojom::URLResponseHeadPtr response,
                         mojo::ScopedDataPipeConsumerHandle,
                         std::optional<mojo_base::BigBuffer>) {
    head = std::move(response);
  }

void ClientSink::OnReceiveRedirect(const net::RedirectInfo& redirect,
                         network::mojom::URLResponseHeadPtr) {
    redirect_url = redirect.new_url;
  }

void ClientSink::OnUploadProgress(int64_t, int64_t,
                        OnUploadProgressCallback callback) {
    std::move(callback).Run();
  }

void ClientSink::OnTransferSizeUpdated(int32_t) {}

void ClientSink::OnComplete(const network::URLLoaderCompletionStatus& status) {
    completed = status.error_code;
    ++completion_count;
  }

ClientSink::ClientSink() = default;

ClientSink::~ClientSink() = default;

NativeLoader::NativeLoader(mojo::PendingReceiver<network::mojom::URLLoader> receiver,
                mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                const network::ResourceRequest& request)
      : headers(request.headers), exempt_headers(request.cors_exempt_headers),
        receiver_(this, std::move(receiver)), client_(std::move(client)) {
    receiver_.set_disconnect_handler(base::BindOnce(
        &NativeLoader::OnDisconnect, base::Unretained(this)));
  }

void NativeLoader::FollowRedirect(network::HttpRequestHeadersUpdateParams changes,
                       const std::optional<GURL>& url) {
    changes.Apply(headers, exempt_headers);
    override_url = url;
    followed = true;
  }

void NativeLoader::SetPriority(net::RequestPriority value, int32_t intra) {
    priority = value;
    intra_priority = intra;
  }

void NativeLoader::Redirect(const GURL& url) {
    net::RedirectInfo info;
    info.new_url = url;
    info.status_code = 302;
    info.new_method = "GET";
    client_->OnReceiveRedirect(info, Response("302 Found"));
  }

void NativeLoader::Respond() {
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    ASSERT_EQ(MOJO_RESULT_OK, mojo::CreateDataPipe(nullptr, producer, consumer));
    producer.reset();
    client_->OnReceiveResponse(Response(), std::move(consumer), std::nullopt);
  }

void NativeLoader::Complete() {
    client_->OnComplete(network::URLLoaderCompletionStatus(net::OK));
  }

void NativeLoader::DisconnectClient() { client_.reset(); }

void NativeLoader::OnDisconnect() { disconnected = true; }

NativeLoader::~NativeLoader() = default;

RecordingFactory::RecordingFactory(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> next) : next_(std::move(next)) {}

void RecordingFactory::CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t id, uint32_t options, const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) {
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

void RecordingFactory::Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> r) {
    receivers_.Add(this, std::move(r));
  }

std::unique_ptr<network::PendingSharedURLLoaderFactory> RecordingFactory::Clone() {
    NOTREACHED();
  }

RecordingFactory::~RecordingFactory() = default;

void DeveloperNetworkFactoryProxyTest::SetUp() {
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

void DeveloperNetworkFactoryProxyTest::TearDown() {
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

void DeveloperNetworkFactoryProxyTest::Save() {
    ASSERT_TRUE(helper_->SaveProfile(origin_, profile_));
    UpdateDeveloperProfileNetworkState(*contents_, origin_.GetURL(),
                                       helper_->GetProfile(origin_));
  }

scoped_refptr<network::SharedURLLoaderFactory> DeveloperNetworkFactoryProxyTest::Build(
      bool document,
      std::optional<int64_t> navigation_id,
      bool later_interceptor) {
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

network::ResourceRequest DeveloperNetworkFactoryProxyTest::Request() {
    network::ResourceRequest request;
    request.url = origin_.GetURL().Resolve("resource.css");
    // Primary-frame fetches carry this flag independently of navigation mode.
    request.is_outermost_main_frame = true;
    request.request_initiator = origin_;
    return request;
  }

void DeveloperNetworkFactoryProxyTest::Load(network::SharedURLLoaderFactory& factory, ClientSink& client,
            const network::ResourceRequest& request) {
    factory.CreateLoaderAndStart(loader_.BindNewPipeAndPassReceiver(), 7, 0,
                                 request, client.Bind(),
                                 net::MutableNetworkTrafficAnnotationTag());
    environment_.RunUntilIdle();
  }

void DeveloperNetworkFactoryProxyTest::UseSecrets() {
    profile_.header_rules[0].value.clear();
    profile_.header_rules[0].secret_reference = "ahoi-keychain:test-request";
    profile_.response_header_rules[0].value.clear();
    profile_.response_header_rules[0].secret_reference =
        "ahoi-keychain:test-response";
    Save();
  }

void DeveloperNetworkFactoryProxyTest::QueueLoad(network::SharedURLLoaderFactory& factory, ClientSink& client) {
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

DeveloperNetworkFactoryProxyTest::DeveloperNetworkFactoryProxyTest() = default;

DeveloperNetworkFactoryProxyTest::~DeveloperNetworkFactoryProxyTest() = default;

}  // namespace ahoi::test
