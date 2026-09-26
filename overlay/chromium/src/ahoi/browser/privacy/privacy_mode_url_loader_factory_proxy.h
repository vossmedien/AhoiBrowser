// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_FACTORY_PROXY_H_
#define AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_FACTORY_PROXY_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/gurl.h"

class PrefService;

namespace net {
class IsolationInfo;
}  // namespace net

namespace url {
class Origin;
}  // namespace url

namespace ahoi::privacy {

// Handoff 066: the top-level site whose mode governs a subresource factory.
// Frames, dedicated/shared workers and (storage-partitioned) service workers
// all carry the embedding top-level origin in their IsolationInfo; without
// one (unpartitioned service worker) the factory's own origin decides. A
// service worker therefore follows the site it runs for, not the page it
// happens to control: the conservative choice, since a strict site's worker
// never loses the rules because a compatibility page uses it.
GURL PolicySiteForSubresourceFactory(const net::IsolationInfo& isolation_info,
                                     const url::Origin& request_initiator);

// Appends PrivacyModeURLLoaderFactoryProxy to `factory_builder` when the
// factory serves subresources of a strict document, and does nothing in the
// default mode (no extra hop, PRIV-17). The mode is a snapshot at factory
// creation, i.e. per committed document: a changed mode applies from the next
// load. Off the record, per-site exceptions are not consulted
// (GetPolicySnapshot). Call it before the extension webRequest proxy is
// appended, so webRequest (uBO Classic) sees and can block the request with
// its strict-mode headers.
void MaybeProxyPrivacyModeURLLoaderFactory(
    const PrefService* prefs,
    bool is_off_the_record,
    bool is_subresource_factory,
    const net::IsolationInfo& isolation_info,
    const url::Origin& request_initiator,
    network::URLLoaderFactoryBuilder& factory_builder);

class PrivacyModeURLLoaderFactoryProxy final
    : public network::SelfDeletingURLLoaderFactory {
 public:
  PrivacyModeURLLoaderFactoryProxy(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>
          target_factory_remote,
      base::SelfDeletingPassKey pass_key);
  PrivacyModeURLLoaderFactoryProxy(const PrivacyModeURLLoaderFactoryProxy&) =
      delete;
  PrivacyModeURLLoaderFactoryProxy& operator=(
      const PrivacyModeURLLoaderFactoryProxy&) = delete;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;

 private:
  ~PrivacyModeURLLoaderFactoryProxy() override;
  void OnTargetFactoryError();

  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
};

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_FACTORY_PROXY_H_
