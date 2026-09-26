// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_url_loader_factory_proxy.h"

#include <utility>

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "ahoi/browser/privacy/privacy_strict_request_rules.h"
#include "base/functional/bind.h"
#include "base/types/pass_key.h"
#include "components/prefs/pref_service.h"
#include "net/base/isolation_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/origin.h"

namespace ahoi::privacy {

GURL PolicySiteForSubresourceFactory(const net::IsolationInfo& isolation_info,
                                     const url::Origin& request_initiator) {
  if (const std::optional<url::Origin>& top = isolation_info.top_frame_origin();
      top.has_value() && !top->opaque()) {
    return top->GetURL();
  }
  return request_initiator.GetURL();
}

void MaybeProxyPrivacyModeURLLoaderFactory(
    const PrefService* prefs,
    bool is_off_the_record,
    bool is_subresource_factory,
    const net::IsolationInfo& isolation_info,
    const url::Origin& request_initiator,
    network::URLLoaderFactoryBuilder& factory_builder) {
  // Navigations, worker and service-worker scripts, prefetch and downloads
  // pass PrivacyModeURLLoaderThrottle already.
  if (!prefs || !is_subresource_factory) {
    return;
  }
  const GURL site =
      PolicySiteForSubresourceFactory(isolation_info, request_initiator);
  if (!site.SchemeIsHTTPOrHTTPS() ||
      !GetPolicySnapshot(*prefs, is_off_the_record).IsStrictForUrl(site)) {
    return;
  }
  auto [receiver, remote] = factory_builder.Append();
  base::MakeSelfDeleting<PrivacyModeURLLoaderFactoryProxy>(std::move(receiver),
                                                           std::move(remote));
}

PrivacyModeURLLoaderFactoryProxy::PrivacyModeURLLoaderFactoryProxy(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory_remote,
    base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(loader_receiver),
                                            pass_key) {
  target_factory_.Bind(std::move(target_factory_remote));
  target_factory_.set_disconnect_handler(
      base::BindOnce(&PrivacyModeURLLoaderFactoryProxy::OnTargetFactoryError,
                     base::Unretained(this)));
}

PrivacyModeURLLoaderFactoryProxy::~PrivacyModeURLLoaderFactoryProxy() =
    default;

void PrivacyModeURLLoaderFactoryProxy::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  network::ResourceRequest strict_request = request;
  if (strict_request.url.SchemeIsHTTPOrHTTPS()) {
    ApplyStrictRequestRules(strict_request, /*is_main_frame=*/false);
  }
  // Every request is forwarded; blocking stays with the next interceptor
  // (webRequest) or the network service.
  target_factory_->CreateLoaderAndStart(
      std::move(loader_receiver), request_id, options, strict_request,
      std::move(client), traffic_annotation);
}

void PrivacyModeURLLoaderFactoryProxy::OnTargetFactoryError() {
  DisconnectReceiversAndDestroy();
}

}  // namespace ahoi::privacy
