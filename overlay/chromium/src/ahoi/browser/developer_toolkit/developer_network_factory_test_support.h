// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_TEST_SUPPORT_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_TEST_SUPPORT_H_

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

namespace ahoi::test {

network::mojom::URLResponseHeadPtr Response(const char* status = "200 OK");

struct SecretFixture {
  std::atomic<int> request_reads = 0;
  bool resolve_response = true;
  base::WaitableEvent entered;
  base::WaitableEvent allow_read{base::WaitableEvent::ResetPolicy::MANUAL,
                                base::WaitableEvent::InitialState::SIGNALED};
};

class FixtureSecretStore final : public DeveloperSecretStore {
 public:
  ~FixtureSecretStore() override;
  explicit FixtureSecretStore(std::shared_ptr<SecretFixture> fixture);
  std::optional<std::string> Store(std::string_view,
                                   std::string_view) override;
  bool Remove(std::string_view) override;
  std::optional<std::string> Resolve(std::string_view reference) const override;

 private:
  const std::shared_ptr<SecretFixture> fixture_;
};

class ClientSink final : public network::mojom::URLLoaderClient {
 public:
  ClientSink();
  ~ClientSink() override;
  mojo::PendingRemote<network::mojom::URLLoaderClient> Bind();
  void Disconnect();
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr) override;
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr response,
                         mojo::ScopedDataPipeConsumerHandle,
                         std::optional<mojo_base::BigBuffer>) override;
  void OnReceiveRedirect(const net::RedirectInfo& redirect,
                         network::mojom::URLResponseHeadPtr) override;
  void OnUploadProgress(int64_t, int64_t,
                        OnUploadProgressCallback callback) override;
  void OnTransferSizeUpdated(int32_t) override;
  void OnComplete(const network::URLLoaderCompletionStatus& status) override;
  network::mojom::URLResponseHeadPtr head;
  GURL redirect_url;
  std::optional<int> completed;
  int completion_count = 0;

 private:
  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
};

class NativeLoader final : public network::mojom::URLLoader {
 public:
  ~NativeLoader() override;
  NativeLoader(mojo::PendingReceiver<network::mojom::URLLoader> receiver,
                mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                const network::ResourceRequest& request);
  void FollowRedirect(network::HttpRequestHeadersUpdateParams changes,
                       const std::optional<GURL>& url) override;
  void SetPriority(net::RequestPriority value, int32_t intra) override;
  void Redirect(const GURL& url);
  void Respond();
  void Complete();
  void DisconnectClient();
  net::HttpRequestHeaders headers;
  net::HttpRequestHeaders exempt_headers;
  std::optional<GURL> override_url;
  bool followed = false;
  bool disconnected = false;
  net::RequestPriority priority = net::IDLE;
  int32_t intra_priority = -1;

 private:
  void OnDisconnect();
  mojo::Receiver<network::mojom::URLLoader> receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
};

// This is a later extension interceptor or the terminal native factory.
// Its IPC contract exercises the production adapter, not a second algorithm.
class RecordingFactory final : public network::SharedURLLoaderFactory {
 public:
  explicit RecordingFactory(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> next =
          mojo::NullRemote());
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t id, uint32_t options, const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override;
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> r) override;
  std::unique_ptr<network::PendingSharedURLLoaderFactory> Clone() override;
  std::vector<network::ResourceRequest> seen;
  std::vector<std::unique_ptr<NativeLoader>> loaders;
  bool block = false;

 private:
  ~RecordingFactory() override;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> next_;
};

class DeveloperNetworkFactoryProxyTest : public testing::Test {
 protected:
  DeveloperNetworkFactoryProxyTest();
  ~DeveloperNetworkFactoryProxyTest() override;
  void SetUp() override;
  void TearDown() override;
  void Save();
  scoped_refptr<network::SharedURLLoaderFactory> Build(
      bool document = true,
      std::optional<int64_t> navigation_id = std::nullopt,
      bool later_interceptor = true);
  network::ResourceRequest Request();
  void Load(network::SharedURLLoaderFactory& factory, ClientSink& client,
            const network::ResourceRequest& request);
  void UseSecrets();
  void QueueLoad(network::SharedURLLoaderFactory& factory, ClientSink& client);
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

}  // namespace ahoi::test

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_TEST_SUPPORT_H_
