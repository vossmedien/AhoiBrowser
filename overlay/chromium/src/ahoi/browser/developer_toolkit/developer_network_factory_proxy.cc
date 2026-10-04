// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_network_factory_proxy.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/self_deleting.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
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

uint64_t ActivationGeneration(content::WebContents* contents) {
  const auto* helper = DeveloperProfileTabHelper::FromWebContents(contents);
  return helper ? helper->activation_generation() : 0;
}

std::optional<DeveloperProfile> ResolveRequestSecrets(
    DeveloperSecretStoreFactory factory,
    DeveloperProfile source) {
  auto store = factory.Run();
  return store ? MaterializeDeveloperProfileHeaderSecrets(
                     std::move(source), *store)
               : std::nullopt;
}

// Both control and response pipes are forwarded. In particular FollowRedirect
// must restore our original headers before a new origin sees the request.
class DeveloperRequestProxy final : public network::mojom::URLLoader,
                                    public network::mojom::URLLoaderClient {
 public:
  DeveloperRequestProxy(
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      std::unique_ptr<DeveloperProfileURLLoaderThrottle> throttle,
      base::RepeatingCallback<bool()> still_allowed)
      : client_(std::move(client)), throttle_(std::move(throttle)),
        still_allowed_(std::move(still_allowed)) {
    client_.set_disconnect_handler(base::BindOnce(
        &DeveloperRequestProxy::Close, weak_factory_.GetWeakPtr()));
  }

  void Start(
      mojo::SelfOwnedReceiverRef<network::mojom::URLLoader> owner,
      network::mojom::URLLoaderFactory& factory,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      const net::MutableNetworkTrafficAnnotationTag& annotation,
      std::optional<DeveloperProfile> secret_source = std::nullopt,
      DeveloperSecretStoreFactory secret_factory = {}) {
    owner_ = std::move(owner);
    if (secret_source) {
      // Own the native chain while this request awaits Keychain I/O. Only
      // independent store handles and value metadata cross to the worker.
      factory.Clone(pending_factory_.BindNewPipeAndPassReceiver());
      pending_factory_.set_disconnect_handler(base::BindOnce(
          &DeveloperRequestProxy::OnNativeDisconnect,
          weak_factory_.GetWeakPtr()));
      const bool posted = base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
          base::BindOnce(&ResolveRequestSecrets, std::move(secret_factory),
                         std::move(*secret_source)),
          base::BindOnce(&DeveloperRequestProxy::SecretsResolved,
                         weak_factory_.GetWeakPtr(), request_id, options,
                         request, annotation));
      if (!posted) {
        Serve(*pending_factory_.get(), request_id, options, request, annotation);
      }
      return;
    }
    Serve(factory, request_id, options, request, annotation);
  }

  void Serve(network::mojom::URLLoaderFactory& factory,
             int32_t request_id,
             uint32_t options,
             const network::ResourceRequest& request,
             const net::MutableNetworkTrafficAnnotationTag& annotation) {
    response_url_ = request.url;
    network::ResourceRequest modified = request;
    bool defer = false;
    if (throttle_) {
      throttle_->WillStartRequest(&modified, &defer);
    }
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
    if (pending_priority_) {
      native_loader_->SetPriority(pending_priority_->first,
                                  pending_priority_->second);
      pending_priority_.reset();
    }
  }

  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams params,
      const std::optional<GURL>& new_url) override {
    if (!native_loader_.is_bound()) {
      OnComplete(network::URLLoaderCompletionStatus(net::ERR_UNEXPECTED));
      return;
    }
    if (redirect_ && throttle_) {
      net::RedirectInfo redirect = *redirect_;
      if (new_url) {
        redirect.new_url = *new_url;
      }
      network::HttpRequestHeadersUpdateParams own_changes;
      bool defer = false;
      if (!OverridesStillAllowed()) {
        throttle_->RestoreOriginalHeadersForRedirect(own_changes);
        throttle_.reset();
        still_allowed_.Reset();
      } else {
        throttle_->WillRedirectRequest(&redirect, *redirect_head_, &defer,
                                      &own_changes);
      }
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
    if (!native_loader_.is_bound()) {
      pending_priority_ = std::make_pair(priority, intra_priority_value);
      return;
    }
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
    if (throttle_ && !OverridesStillAllowed()) {
      throttle_.reset();
      still_allowed_.Reset();
    }
    if (throttle_) {
      throttle_->WillProcessResponse(response_url_, head.get(), &defer);
    }
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
  bool OverridesStillAllowed() const {
    return still_allowed_ && still_allowed_.Run();
  }

  void SecretsResolved(
      int32_t request_id,
      uint32_t options,
      network::ResourceRequest request,
      net::MutableNetworkTrafficAnnotationTag annotation,
      std::optional<DeveloperProfile> materialized) {
    if (!OverridesStillAllowed()) {
      throttle_.reset();
      still_allowed_.Reset();
    } else if (materialized) {
      throttle_ = std::make_unique<DeveloperProfileURLLoaderThrottle>(
          url::Origin::Create(request.url), std::move(*materialized));
    }
    // Failed resolution retains only the prior fail-closed throttle (e.g.
    // cache/UA). Resolved values belong solely to this live request adapter.
    Serve(*pending_factory_.get(), request_id, options, request, annotation);
  }
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
  mojo::Remote<network::mojom::URLLoaderFactory> pending_factory_;
  mojo::Receiver<network::mojom::URLLoaderClient> native_client_receiver_{this};
  mojo::SelfOwnedReceiverRef<network::mojom::URLLoader> owner_;
  std::unique_ptr<DeveloperProfileURLLoaderThrottle> throttle_;
  base::RepeatingCallback<bool()> still_allowed_;
  GURL response_url_;
  std::optional<net::RedirectInfo> redirect_;
  network::mojom::URLResponseHeadPtr redirect_head_;
  bool completed_ = false;
  std::optional<std::pair<net::RequestPriority, int32_t>> pending_priority_;
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
      DeveloperSecretStoreFactory secret_store_factory,
      base::SelfDeletingPassKey key)
      : network::SelfDeletingURLLoaderFactory(std::move(receiver), key),
        target_(std::move(target)), contents_(std::move(contents)),
        origin_(std::move(origin)), frame_token_(frame_token),
        navigation_id_(navigation_id),
        activation_generation_(ActivationGeneration(contents_.get())),
        secret_store_factory_(std::move(secret_store_factory)) {
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
    std::unique_ptr<DeveloperProfileURLLoaderThrottle> throttle;
    auto profile = CurrentSnapshot(request, /*verify_saved_rules=*/true);
    if (profile) {
      // The typed throttle applies the same unresolved-secret fail-closed
      // policy; CurrentSnapshot already validated this request's native owner.
      throttle = std::make_unique<DeveloperProfileURLLoaderThrottle>(
          url::Origin::Create(request.url), *profile);
    }
    const bool needs_secrets = profile &&
        DeveloperProfileHasActiveHeaderSecretReferences(*profile);
    if ((!throttle && !needs_secrets) || !loader.is_valid() ||
        !client.is_valid()) {
      target_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                    request, std::move(client), annotation);
      return;
    }
    base::RepeatingCallback<bool()> still_allowed;
    if (profile) {
      still_allowed = base::BindRepeating(
          [](base::WeakPtr<DeveloperFactoryProxy> factory,
             network::ResourceRequest request, DeveloperProfile source) {
            return factory && factory->CurrentSnapshot(
                                  request, /*verify_saved_rules=*/true) == source;
          }, weak_factory_.GetWeakPtr(), request, *profile);
    }
    auto proxy = std::make_unique<DeveloperRequestProxy>(
        std::move(client), std::move(throttle), std::move(still_allowed));
    DeveloperRequestProxy* const request_proxy = proxy.get();
    auto owner = mojo::MakeSelfOwnedReceiver<network::mojom::URLLoader>(
        std::move(proxy), std::move(loader));
    if (owner) {
      request_proxy->Start(std::move(owner), *target_.get(), request_id,
                          options, request, annotation,
                          needs_secrets ? std::move(profile) : std::nullopt,
                          secret_store_factory_);
    }
  }

 private:
  std::optional<DeveloperProfile> CurrentSnapshot(
      const network::ResourceRequest& request,
      bool verify_saved_rules = false) {
    auto* frame = content::RenderFrameHost::FromFrameToken(frame_token_);
    VLOG(1) << "Ahoi developer factory ownership: contents=" << !!contents_
            << " frame=" << !!frame
            << " active=" << (frame && frame->IsActive())
            << " generation="
            << (contents_ && ActivationGeneration(contents_.get()) ==
                                 activation_generation_)
            << " navigation="
            << (frame && frame->GetNavigationId() == navigation_id_)
            << " origin="
            << (frame && frame->GetLastCommittedOrigin() == origin_)
            << " owner="
            << (frame && content::WebContents::FromRenderFrameHost(frame) ==
                             contents_.get())
            << " main=" << request.is_outermost_main_frame
            << " initiator=" << (request.request_initiator == origin_)
            << " committed="
            << (contents_ &&
                url::Origin::Create(contents_->GetLastCommittedURL()) == origin_);
    if (contents_ && frame && frame->IsActive() &&
        ActivationGeneration(contents_.get()) == activation_generation_ &&
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
          auto snapshot = GetDeveloperProfileNetworkSnapshotForRequest(
              request, prefs, false, contents_.get());
          if (!verify_saved_rules) {
            return snapshot;
          }
          auto current = GetDeveloperNetworkProfileForTab(
              prefs, contents_.get(), origin_.GetURL());
          auto saved = current ? std::optional<DeveloperProfile>(
                                     MakeDeveloperProfileNetworkSnapshot(*current))
                               : std::nullopt;
          if (saved && url::Origin::Create(request.url) != origin_) {
            saved = saved->cache_disabled
                        ? std::optional<DeveloperProfile>(
                              DeveloperProfile{.cache_disabled = true})
                        : std::nullopt;
          }
          if (snapshot && saved && *snapshot == *saved) {
            return snapshot;
          }
        }
      }
    }
    return std::nullopt;
  }

  ~DeveloperFactoryProxy() override = default;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
  base::WeakPtr<content::WebContents> contents_;
  const url::Origin origin_;
  const content::GlobalRenderFrameHostToken frame_token_;
  const int64_t navigation_id_;
  const uint64_t activation_generation_;
  const DeveloperSecretStoreFactory secret_store_factory_;
  base::WeakPtrFactory<DeveloperFactoryProxy> weak_factory_{this};
};

}  // namespace

void MaybeProxyDeveloperProfileURLLoaderFactory(
    PrefService* prefs,
    bool is_off_the_record,
    bool is_frame_owned_subresource_factory,
    content::RenderFrameHost* frame,
    std::optional<int64_t> navigation_id,
    const url::Origin& factory_origin,
    network::URLLoaderFactoryBuilder& builder,
    DeveloperSecretStoreFactory secret_store_factory) {
  VLOG(1) << "Ahoi developer factory eligibility: toolkit="
          << ToolkitEnabled(prefs) << " otr=" << is_off_the_record
          << " frame_owned=" << is_frame_owned_subresource_factory
          << " frame=" << !!frame
          << " http=" << factory_origin.GetURL().SchemeIsHTTPOrHTTPS();
  if (!ToolkitEnabled(prefs) || is_off_the_record ||
      !is_frame_owned_subresource_factory || !frame ||
      !factory_origin.GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  if (!web_contents) {
    return;
  }
  const auto profile = GetDeveloperNetworkProfileForTab(
      prefs, web_contents, factory_origin.GetURL());
  VLOG(1) << "Ahoi developer factory profile: found=" << !!profile
          << " ua=" << (profile && profile->user_agent_enabled)
          << " request=" << (profile && profile->header_rules_enabled)
          << " response=" << (profile && profile->response_header_rules_enabled)
          << " cache=" << (profile && profile->cache_disabled);
  if (!profile || (!profile->user_agent_enabled &&
                   !profile->header_rules_enabled &&
                   !profile->response_header_rules_enabled &&
                   !profile->cache_disabled)) {
    return;
  }
  auto [receiver, remote] = builder.Append();
  if (!secret_store_factory) {
    secret_store_factory =
        base::BindRepeating(&CreatePlatformDeveloperSecretStore);
  }
  base::MakeSelfDeleting<DeveloperFactoryProxy>(
      std::move(receiver), std::move(remote), web_contents->GetWeakPtr(),
      factory_origin, frame->GetGlobalFrameToken(),
      navigation_id.value_or(frame->GetNavigationId()),
      std::move(secret_store_factory));
}

}  // namespace ahoi
