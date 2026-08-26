// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_mode_url_loader_throttle.h"

#include <memory>
#include <string_view>
#include <utility>

#include "components/prefs/pref_service.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/referrer_policy.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/origin.h"

namespace ahoi::privacy {

namespace {

constexpr std::string_view kHighEntropyUaClientHintHeaders[] = {
    "Sec-CH-UA-Arch",
    "Sec-CH-UA-Bitness",
    "Sec-CH-UA-Form-Factors",
    "Sec-CH-UA-Full-Version",
    "Sec-CH-UA-Full-Version-List",
    "Sec-CH-UA-Model",
    "Sec-CH-UA-Platform-Version",
    "Sec-CH-UA-WoW64",
};

GURL PolicyOriginForRequest(const network::ResourceRequest& request) {
  if (request.is_outermost_main_frame) {
    return request.url;
  }
  if (request.request_initiator) {
    return request.request_initiator->GetURL();
  }
  return request.url;
}

}  // namespace

std::unique_ptr<blink::URLLoaderThrottle>
MaybeCreatePrivacyModeURLLoaderThrottle(const network::ResourceRequest& request,
                                        const PrefService* prefs,
                                        bool is_off_the_record) {
  if (!prefs || !request.url.SchemeIsHTTPOrHTTPS()) {
    return nullptr;
  }
  PrivacyPolicy policy = GetPolicySnapshot(*prefs, is_off_the_record);
  if (!policy.IsStrictForUrl(PolicyOriginForRequest(request))) {
    return nullptr;
  }
  return std::make_unique<PrivacyModeURLLoaderThrottle>(
      std::move(policy), PolicyOriginForRequest(request),
      request.site_for_cookies, request.is_outermost_main_frame);
}

PrivacyModeURLLoaderThrottle::PrivacyModeURLLoaderThrottle(
    PrivacyPolicy policy,
    GURL policy_origin,
    net::SiteForCookies site_for_cookies,
    bool is_main_frame)
    : policy_(std::move(policy)),
      policy_origin_(std::move(policy_origin)),
      site_for_cookies_(std::move(site_for_cookies)),
      is_main_frame_(is_main_frame) {}

PrivacyModeURLLoaderThrottle::~PrivacyModeURLLoaderThrottle() = default;

void PrivacyModeURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* /*defer*/) {
  if (!request || !request->url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  ApplyToRequest(*request);
}

void PrivacyModeURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& /*response_head*/,
    bool* /*defer*/,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  if (!redirect_info || !headers_update_params ||
      !redirect_info->new_url.SchemeIsHTTPOrHTTPS()) {
    if (headers_update_params) {
      headers_update_params->removed_headers.push_back("Sec-GPC");
    }
    return;
  }
  const GURL policy_origin =
      is_main_frame_ ? redirect_info->new_url : policy_origin_;
  if (!policy_.IsStrictForUrl(policy_origin)) {
    headers_update_params->removed_headers.push_back("Sec-GPC");
    return;
  }
  headers_update_params->modified_headers.SetHeader("Sec-GPC", "1");
  if (IsThirdPartyRequest(redirect_info->new_url)) {
    for (std::string_view header : kHighEntropyUaClientHintHeaders) {
      headers_update_params->removed_headers.emplace_back(header);
    }
  }
  if (is_main_frame_) {
    redirect_info->new_url =
        StripKnownTrackingParameters(redirect_info->new_url);
  }
}

const char* PrivacyModeURLLoaderThrottle::NameForLoggingWillStartRequest() {
  return "AhoiPrivacyModeURLLoaderThrottle";
}

void PrivacyModeURLLoaderThrottle::ApplyToRequest(
    network::ResourceRequest& request) const {
  const PrivacyMode mode = policy_.ModeForUrl(PolicyOriginForRequest(request));
  if (mode != PrivacyMode::kStrict) {
    return;
  }
  request.headers.SetHeader("Sec-GPC", "1");
  if (IsThirdPartyRequest(request.url)) {
    for (std::string_view header : kHighEntropyUaClientHintHeaders) {
      request.headers.RemoveHeader(header);
    }
  }
  if (is_main_frame_) {
    request.url = StripKnownTrackingParameters(request.url);
  }

  // Preserve the destination host while removing path/query detail from a
  // cross-origin referrer. Chromium remains authoritative for the final
  // Referrer-Policy header and can still tighten this further.
  if (request.referrer.is_valid() && url::Origin::Create(request.referrer) !=
                                         url::Origin::Create(request.url)) {
    request.referrer = url::Origin::Create(request.referrer).GetURL();
    request.referrer_policy = net::ReferrerPolicy::ORIGIN;
  }
}

bool PrivacyModeURLLoaderThrottle::IsThirdPartyRequest(const GURL& url) const {
  return !is_main_frame_ && !site_for_cookies_.IsFirstParty(url);
}

}  // namespace ahoi::privacy
