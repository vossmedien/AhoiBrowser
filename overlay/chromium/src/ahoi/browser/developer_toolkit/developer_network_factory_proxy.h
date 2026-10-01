// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_PROXY_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_PROXY_H_

#include <cstdint>
#include <optional>

class PrefService;
namespace content {
class RenderFrameHost;
}
namespace network {
class URLLoaderFactoryBuilder;
}
namespace url {
class Origin;
}

namespace ahoi {

// Renderer document subresources do not pass ContentBrowserClient's browser
// URLLoaderThrottle hook. Append a request-local adapter only for a configured
// normal document. The native factory, CORS checks and later webRequest proxy
// remain authoritative. Unknown worker owners are not guessed from a process.
// Every request rechecks the live tab/origin and toolkit enablement; closed,
// navigated, off-the-record or disabled contexts receive no override.
void MaybeProxyDeveloperProfileURLLoaderFactory(
    PrefService* prefs,
    bool is_off_the_record,
    bool is_document_subresource_factory,
    content::RenderFrameHost* frame,
    std::optional<int64_t> navigation_id,
    const url::Origin& factory_origin,
    network::URLLoaderFactoryBuilder& factory_builder);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_NETWORK_FACTORY_PROXY_H_
