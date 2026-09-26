// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_strict_request_rules.h"

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "net/http/http_request_headers.h"
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

}  // namespace

base::span<const std::string_view> HighEntropyUaClientHintHeaders() {
  return kHighEntropyUaClientHintHeaders;
}

net::ReferrerPolicy CapReferrerPolicyForStrictMode(net::ReferrerPolicy policy) {
  switch (policy) {
    case net::ReferrerPolicy::NEVER_CLEAR:
    case net::ReferrerPolicy::CLEAR_ON_TRANSITION_FROM_SECURE_TO_INSECURE:
    case net::ReferrerPolicy::ORIGIN_ONLY_ON_TRANSITION_CROSS_ORIGIN:
      return net::ReferrerPolicy::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN;
    default:
      return policy;
  }
}

bool IsThirdPartyForStrictMode(const network::ResourceRequest& request,
                               bool is_main_frame) {
  return !is_main_frame && !request.site_for_cookies.IsFirstParty(request.url);
}

void ApplyStrictRequestRules(network::ResourceRequest& request,
                             bool is_main_frame) {
  request.headers.SetHeader("Sec-GPC", "1");
  if (IsThirdPartyForStrictMode(request, is_main_frame)) {
    for (std::string_view header : kHighEntropyUaClientHintHeaders) {
      request.headers.RemoveHeader(header);
    }
  }
  // Preserve the destination host while removing path/query detail from a
  // cross-origin referrer. Chromium remains authoritative for the final
  // Referrer-Policy header and can still tighten this further.
  if (request.referrer.is_valid() && url::Origin::Create(request.referrer) !=
                                         url::Origin::Create(request.url)) {
    request.referrer = url::Origin::Create(request.referrer).GetURL();
    request.referrer_policy = net::ReferrerPolicy::ORIGIN;
    return;
  }
  request.referrer_policy = CapReferrerPolicyForStrictMode(request.referrer_policy);
}

bool GlobalPrivacyControlForMainFrameUrl(const PrefService& prefs,
                                         bool is_off_the_record,
                                         const GURL& main_frame_url) {
  return main_frame_url.SchemeIsHTTPOrHTTPS() &&
         GetPolicySnapshot(prefs, is_off_the_record)
             .IsStrictForUrl(main_frame_url);
}

}  // namespace ahoi::privacy
