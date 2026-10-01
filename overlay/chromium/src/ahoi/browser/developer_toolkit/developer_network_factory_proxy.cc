// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_proxy.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "base/functional/bind.h"
#include "base/memory/self_deleting.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/net_errors.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/origin.h"

namespace ahoi {
namespace {

bool ToolkitEnabled(const PrefService* prefs) {
  return prefs && developer_toolkit_prefs::IsToolkitEnabled(*prefs);
}

// Both control and response pipes are forwarded. In particular FollowRedirect
// must restore our original headers before a new origin sees the request.
class DeveloperRequestProxy final : public network::mojom::URLLoader,
                                    public network::mojom::URLLoaderClient {
 public:
  DeveloperRequestProxy(
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      std::unique_ptr<blink::URLLoaderThrottle> throttle)
      : client_(std::move(client)), throttle_(std::move(throttle)) {
    client_.set_disconnect_handler(base::BindOnce(
        &DeveloperRequestProxy::Close, weak_factory_.GetWeakPtr()));
  }

  void Start(
      mojo::SelfOwnedReceiverRef<network::mojom::URLLoader> owner,
      network::mojom::URLLoaderFactory& factory,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      const net::MutableNetworkTrafficAnnotationTag& annotation) {
    owner_ = std::move(owner);
    response_url_ = request.url;
    network::ResourceRequest modified = request;
    bool defer = false;
    throttle_->WillStartRequest(&modified, &defer);
    // This adapter uses the existing synchronous developer-profile throttle.
    if (defer) {
      OnComplete(network::URLLoaderCompletionStatus(net::ERR_UNEXPECTED));
      return;
    }
    auto native_client = native_client_receiver_.BindNewPipeAndPassRemote();
    native_client_receiver_.set_disconnect_handler(base::BindOnce(
        &DeveloperRequestProxy::OnNativeDisconnect,
        weak_factory_.GetWeakPtr()));
    factory.CreateLoaderAndStart(
        native_loader_.BindNewPipeAndPassReceiver(), request_id, options,
        modified, std::move(native_client), annotation);
  }

  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams params,
      const std::optional<GURL>& new_url) override {
    if (redirect_) {
      net::RedirectInfo redirect = *redirect_;
      if (new_url) {
        redirect.new_url = *new_url;
      }
      network::HttpRequestHeadersUpdateParams own_changes;
      bool defer = false;
      throttle_->WillRedirectRequest(&redirect, *redirect_head_, &defer,
                                     &own_changes);
      if (defer) {
        OnComplete(network::URLLoaderCompletionStatus(net::ERR_UNEXPECTED));
        return;
      }
      // MergeFrom is not sequential composition: a renderer's pending set
      // would otherwise resurrect a header that our cross-origin rule removes.
      for (const std::string& name : own_changes.removed_headers) {
        params.modified_headers.RemoveHeader(name);
        params.modified_cors_exempt_headers.RemoveHeader(name);
      }
      // A restored original value also owns this name: the second header map
      // must not reintroduce the override after the normal map is restored.
      for (const auto& header : own_changes.modified_headers.GetHeaderVector()) {
        params.modified_cors_exempt_headers.RemoveHeader(header.key);
      }
      params.MergeFrom(std::move(own_changes));
      response_url_ = redirect.new_url;
      redirect_.reset();
      redirect_head_.reset();
    }
    // Native URLLoader still validates the optional redirect URL and CORS.
    native_loader_->FollowRedirect(std::move(params), new_url);
  }

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    native_loader_->SetPriority(priority, intra_priority_value);
  }

  void OnReceiveEarlyHints(
      network::mojom::EarlyHintsPtr early_hints) override {
    client_->OnReceiveEarlyHints(std::move(early_hints));
  }

  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> metadata) override {
    bool defer = false;
    throttle_->WillProcessResponse(response_url_, head.get(), &defer);
    if (defer) {
      OnComplete(network::URLLoaderCompletionStatus(net::ERR_UNEXPECTED));
      return;
    }
    client_->OnReceiveResponse(std::move(head), std::move(body),
                               std::move(metadata));
  }

  void OnReceiveRedirect(
      const net::RedirectInfo& redirect,
      network::mojom::URLResponseHeadPtr head) override {
    redirect_ = redirect;
    redirect_head_ = head.Clone();
    client_->OnReceiveRedirect(redirect, std::move(head));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    client_->OnUploadProgress(current_position, total_size,
                              std::move(callback));
  }

  void OnTransferSizeUpdated(int32_t difference) override {
    client_->OnTransferSizeUpdated(difference);
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    if (completed_) {
      return;
    }
    completed_ = true;
    client_->OnComplete(status);
    Close();
  }

 private:
  void OnNativeDisconnect() {
    // Completion and this disconnect use the same client pipe, preserving
    // their order. A loader-control disconnect alone is not an error.
    if (!completed_) {
      OnComplete(network::URLLoaderCompletionStatus(net::ERR_ABORTED));
    }
  }
  void Close() {
    if (owner_) {
      owner_->Close();
    }
  }

  mojo::Remote<network::mojom::URLLoaderClient> client_;
  mojo::Remote<network::mojom::URLLoader> native_loader_;
  mojo::Receiver<network::mojom::URLLoaderClient> native_client_receiver_{this};
  mojo::SelfOwnedReceiverRef<network::mojom::URLLoader> owner_;
  std::unique_ptr<blink::URLLoaderThrottle> throttle_;
  GURL response_url_;
  std::optional<net::RedirectInfo> redirect_;
  network::mojom::URLResponseHeadPtr redirect_head_;
  bool completed_ = false;
  base::WeakPtrFactory<DeveloperRequestProxy> weak_factory_{this};
};

class DeveloperFactoryProxy final
    : public network::SelfDeletingURLLoaderFactory {
 public:
  DeveloperFactoryProxy(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
      base::WeakPtr<content::WebContents> contents,
      url::Origin origin,
      content::GlobalRenderFrameHostToken frame_token,
      int64_t navigation_id,
      base::SelfDeletingPassKey key)
      : network::SelfDeletingURLLoaderFactory(std::move(receiver), key),
        target_(std::move(target)), contents_(std::move(contents)),
        origin_(std::move(origin)), frame_token_(frame_token),
        navigation_id_(navigation_id) {
    target_.set_disconnect_handler(base::BindOnce(
        &DeveloperFactoryProxy::DisconnectReceiversAndDestroy,
        base::Unretained(this)));
  }

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override {
    std::unique_ptr<blink::URLLoaderThrottle> throttle;
    auto* frame = content::RenderFrameHost::FromFrameToken(frame_token_);
    if (contents_ && frame && frame->IsActive() &&
        frame->GetNavigationId() == navigation_id_ &&
        frame->GetLastCommittedOrigin() == origin_ &&
        content::WebContents::FromRenderFrameHost(frame) == contents_.get() &&
        !request.is_outermost_main_frame &&
        request.request_initiator == origin_ &&
        url::Origin::Create(contents_->GetLastCommittedURL()) == origin_) {
      auto* context = contents_->GetBrowserContext();
      if (context && !context->IsOffTheRecord() &&
          user_prefs::UserPrefs::IsInitialized(context)) {
        PrefService* prefs = user_prefs::UserPrefs::Get(context);
        if (ToolkitEnabled(prefs)) {
          throttle = MaybeCreateDeveloperProfileURLLoaderThrottle(
              request, prefs, false, contents_.get());
        }
      }
    }
    if (!throttle || !loader.is_valid() || !client.is_valid()) {
      target_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                    request, std::move(client), annotation);
      return;
    }
    auto proxy = std::make_unique<DeveloperRequestProxy>(
        std::move(client), std::move(throttle));
    DeveloperRequestProxy* const request_proxy = proxy.get();
    auto owner = mojo::MakeSelfOwnedReceiver<network::mojom::URLLoader>(
        std::move(proxy), std::move(loader));
    if (owner) {
      request_proxy->Start(std::move(owner), *target_.get(), request_id,
                            options, request, annotation);
    }
  }

 private:
  ~DeveloperFactoryProxy() override = default;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
  base::WeakPtr<content::WebContents> contents_;
  const url::Origin origin_;
  const content::GlobalRenderFrameHostToken frame_token_;
  const int64_t navigation_id_;
};

}  // namespace

void MaybeProxyDeveloperProfileURLLoaderFactory(
    PrefService* prefs,
    bool is_off_the_record,
    bool is_document_subresource_factory,
    content::RenderFrameHost* frame,
    std::optional<int64_t> navigation_id,
    const url::Origin& factory_origin,
    network::URLLoaderFactoryBuilder& builder) {
  if (!ToolkitEnabled(prefs) || is_off_the_record ||
      !is_document_subresource_factory || !frame ||
      !factory_origin.GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  if (!web_contents) {
    return;
  }
  PrefDeveloperProfileStore store(prefs, false);
  const auto profile = store.Get(factory_origin);
  if (!profile || (!profile->user_agent_enabled &&
                   !profile->header_rules_enabled &&
                   !profile->response_header_rules_enabled &&
                   !profile->cache_disabled)) {
    return;
  }
  auto [receiver, remote] = builder.Append();
  base::MakeSelfDeleting<DeveloperFactoryProxy>(
      std::move(receiver), std::move(remote), web_contents->GetWeakPtr(),
      factory_origin, frame->GetGlobalFrameToken(),
      navigation_id.value_or(frame->GetNavigationId()));
}

}  // namespace ahoi
