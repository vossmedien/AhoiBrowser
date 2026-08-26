// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_URL_LOADER_THROTTLE_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_URL_LOADER_THROTTLE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/origin.h"

class PrefService;
class GURL;

namespace net {
class HttpRequestHeaders;
class HttpResponseHeaders;
}  // namespace net

namespace network {
struct ResourceRequest;
}

namespace content {
class WebContents;
}

namespace ahoi {

// Removes document-only name/assets before a profile crosses a network
// request boundary. Header, UA and cache fields remain unchanged.
DeveloperProfile MakeDeveloperProfileNetworkSnapshot(DeveloperProfile profile);

// Creates a request-local throttle only when the exact HTTP(S) origin has an
// enabled UA/header/cache override. Secret values are accepted only from the
// matching staged navigation snapshot; unresolved references disable both
// header directions atomically. PrefService is never read after detachment.
std::unique_ptr<blink::URLLoaderThrottle>
MaybeCreateDeveloperProfileURLLoaderThrottle(
    const network::ResourceRequest& request,
    PrefService* prefs,
    bool is_off_the_record,
    content::WebContents* web_contents);

// Updates the cheap per-tab network snapshot after a primary commit. This
// keeps subresource creation O(1) and avoids deserializing potentially large
// CSS/JavaScript sources for every request.
void UpdateDeveloperProfileNetworkState(
    content::WebContents& web_contents,
    const GURL& committed_url,
    const std::optional<DeveloperProfile>& profile);

// Stages a materialized header snapshot for one exact primary navigation. The
// source profile contains only persisted values/references; the materialized
// profile is plaintext and is rejected unless every active reference was
// resolved. State is never global and must be cleared with the navigation.
bool StageDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    int64_t navigation_id,
    const GURL& request_url,
    DeveloperProfile source_profile,
    DeveloperProfile materialized_profile);

// Keeps a staged snapshot usable across a same-origin redirect of the same
// navigation. Cross-origin or stale navigation IDs clear it fail-closed.
bool RetargetDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    int64_t navigation_id,
    const GURL& request_url);

// With an ID, removes only that navigation's state. Without one, clears any
// staged plaintext snapshot (used by reset/document teardown boundaries).
void ClearDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    std::optional<int64_t> navigation_id = std::nullopt);

class DeveloperProfileURLLoaderThrottle final
    : public blink::URLLoaderThrottle {
 public:
  DeveloperProfileURLLoaderThrottle(url::Origin origin,
                                    DeveloperProfile profile);
  DeveloperProfileURLLoaderThrottle(const DeveloperProfileURLLoaderThrottle&) =
      delete;
  DeveloperProfileURLLoaderThrottle& operator=(
      const DeveloperProfileURLLoaderThrottle&) = delete;
  ~DeveloperProfileURLLoaderThrottle() override;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  const char* NameForLoggingWillStartRequest() override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;
  void WillProcessResponse(const GURL& response_url,
                           network::mojom::URLResponseHead* response_head,
                           bool* defer) override;
  const char* NameForLoggingWillProcessResponse() override;

 private:
  struct OriginalHeader {
    std::string name;
    std::optional<std::string> value;
  };

  void RememberOriginalHeader(const net::HttpRequestHeaders& headers,
                              std::string_view name);
  void ApplyToHeaders(net::HttpRequestHeaders& headers);
  void RestoreForRedirect(
      network::HttpRequestHeadersUpdateParams& headers_update_params) const;
  void ApplyForRedirect(
      network::HttpRequestHeadersUpdateParams& headers_update_params) const;
  void ApplyToResponseHeaders(net::HttpResponseHeaders& headers) const;

  const url::Origin origin_;
  const DeveloperProfile profile_;
  std::vector<OriginalHeader> original_headers_;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_URL_LOADER_THROTTLE_H_
