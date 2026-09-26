// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_url_loader_factory_proxy.h"

#include <optional>
#include <utility>
#include <vector>

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

namespace ahoi::privacy {
namespace {

// Stands in for a later interceptor on the same chain (extension webRequest)
// or for the terminal network factory: records every request, and can block
// like webRequest does.
class RecordingFactory final : public network::SharedURLLoaderFactory {
 public:
  explicit RecordingFactory(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> next =
          mojo::NullRemote())
      : next_(std::move(next)) {}

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override {
    seen.push_back(request);
    if (block) {
      mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
          ->OnComplete(
              network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      return;
    }
    if (next_) {
      next_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                  request, std::move(client), annotation);
    }
  }
  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> r) override {
    receivers_.Add(this, std::move(r));
  }
  std::unique_ptr<network::PendingSharedURLLoaderFactory> Clone() override {
    NOTREACHED();
  }

  std::vector<network::ResourceRequest> seen;
  bool block = false;

 private:
  ~RecordingFactory() override = default;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> next_;
};

class ClientSink final : public network::mojom::URLLoaderClient {
 public:
  mojo::PendingRemote<network::mojom::URLLoaderClient> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr) override {}
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr,
                         mojo::ScopedDataPipeConsumerHandle,
                         std::optional<mojo_base::BigBuffer>) override {}
  void OnReceiveRedirect(const net::RedirectInfo&,
                         network::mojom::URLResponseHeadPtr) override {}
  void OnUploadProgress(int64_t, int64_t, OnUploadProgressCallback) override {}
  void OnTransferSizeUpdated(int32_t) override {}
  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    completed = status.error_code;
  }
  std::optional<int> completed;

 private:
  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
};

class PrivacyModeURLLoaderFactoryProxyTest : public testing::Test {
 protected:
  PrivacyModeURLLoaderFactoryProxyTest() {
    RegisterProfilePrefs(prefs_.registry());
  }

  // Builds privacy proxy -> `web_request` -> `terminal`, the order of
  // ChromeContentBrowserClient::WillCreateURLLoaderFactory after the seam.
  scoped_refptr<network::SharedURLLoaderFactory> Build(
      bool is_subresource_factory) {
    network::URLLoaderFactoryBuilder builder;
    MaybeProxyPrivacyModeURLLoaderFactory(
        &prefs_, /*is_off_the_record=*/false, is_subresource_factory,
        net::IsolationInfo::CreateForInternalRequest(top_), top_, builder);
    auto [receiver, remote] = builder.Append();
    web_request_ = base::MakeRefCounted<RecordingFactory>(std::move(remote));
    web_request_->Clone(std::move(receiver));
    terminal_ = base::MakeRefCounted<RecordingFactory>();
    return std::move(builder).Finish(terminal_);
  }

  void Load(network::SharedURLLoaderFactory& factory, ClientSink& client) {
    network::ResourceRequest request;
    request.url = GURL("https://tracker.test/pixel");
    request.site_for_cookies = net::SiteForCookies::FromOrigin(top_);
    request.request_initiator = top_;
    request.referrer = GURL("https://site.test/top?secret=1");
    request.referrer_policy = net::ReferrerPolicy::NEVER_CLEAR;
    mojo::PendingRemote<network::mojom::URLLoader> loader;
    factory.CreateLoaderAndStart(loader.InitWithNewPipeAndPassReceiver(), 0, 0,
                                 request, client.Bind(),
                                 net::MutableNetworkTrafficAnnotationTag());
    task_environment_.RunUntilIdle();
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  const url::Origin top_ = url::Origin::Create(GURL("https://site.test/"));
  scoped_refptr<RecordingFactory> web_request_;
  scoped_refptr<RecordingFactory> terminal_;
};

// Handoff 066: webRequest (uBO Classic) sees the strict-mode request and the
// request still reaches the network.
TEST_F(PrivacyModeURLLoaderFactoryProxyTest, WebRequestSeesStrictRequest) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      Build(/*is_subresource_factory=*/true);
  ClientSink client;
  Load(*factory, client);
  ASSERT_EQ(1u, web_request_->seen.size());
  EXPECT_EQ("1",
            web_request_->seen[0].headers.GetHeader("Sec-GPC").value_or(""));
  EXPECT_EQ(GURL("https://site.test/"), web_request_->seen[0].referrer);
  EXPECT_EQ(1u, terminal_->seen.size());
}

// A blocking webRequest listener still blocks; the proxy drops nothing.
TEST_F(PrivacyModeURLLoaderFactoryProxyTest, WebRequestCanStillBlock) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      Build(/*is_subresource_factory=*/true);
  web_request_->block = true;
  ClientSink client;
  Load(*factory, client);
  EXPECT_EQ(std::optional<int>(net::ERR_BLOCKED_BY_CLIENT), client.completed);
  EXPECT_TRUE(terminal_->seen.empty());
}

// PRIV-17: the default mode installs no proxy and rewrites nothing.
TEST_F(PrivacyModeURLLoaderFactoryProxyTest, DefaultModeIsUntouched) {
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      Build(/*is_subresource_factory=*/true);
  ClientSink client;
  Load(*factory, client);
  ASSERT_EQ(1u, web_request_->seen.size());
  EXPECT_FALSE(web_request_->seen[0].headers.HasHeader("Sec-GPC"));
  EXPECT_EQ(GURL("https://site.test/top?secret=1"),
            web_request_->seen[0].referrer);
}

// Navigation and script factories are the throttle's; no double rewriting.
TEST_F(PrivacyModeURLLoaderFactoryProxyTest, NonSubresourceFactoryNotProxied) {
  ASSERT_TRUE(SetGlobalMode(&prefs_, PrivacyMode::kStrict));
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      Build(/*is_subresource_factory=*/false);
  ClientSink client;
  Load(*factory, client);
  ASSERT_EQ(1u, web_request_->seen.size());
  EXPECT_FALSE(web_request_->seen[0].headers.HasHeader("Sec-GPC"));
}

TEST(PrivacyModePolicySiteTest, TopFrameOriginDecides) {
  const url::Origin top = url::Origin::Create(GURL("https://site.test/"));
  const url::Origin frame = url::Origin::Create(GURL("https://embed.test/"));
  EXPECT_EQ(GURL("https://site.test/"),
            PolicySiteForSubresourceFactory(
                net::IsolationInfo::CreateForInternalRequest(top), frame));
  EXPECT_EQ(GURL("https://embed.test/"),
            PolicySiteForSubresourceFactory(net::IsolationInfo(), frame));
}

}  // namespace
}  // namespace ahoi::privacy
