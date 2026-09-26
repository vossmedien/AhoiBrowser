// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_PRIVACY_STRICT_REQUEST_RULES_H_
#define AHOI_BROWSER_PRIVACY_PRIVACY_STRICT_REQUEST_RULES_H_

#include <string_view>

#include "base/containers/span.h"
#include "net/url_request/referrer_policy.h"

namespace network {
struct ResourceRequest;
}  // namespace network

namespace ahoi::privacy {

// The request rules of `Mehr Schutz` (docs/PRIVACY.md, Master 1488-1507),
// shared by the navigation throttle and the subresource factory proxy so the
// two paths cannot diverge (handoff 066). Callers apply them only to requests
// of a strict document; the default mode rewrites nothing.

// High-entropy User-Agent client hints removed from third-party requests.
base::span<const std::string_view> HighEntropyUaClientHintHeaders();

// A page may ask for more referrer detail than strict mode allows. Policies
// that would send a full URL cross-origin (or on a downgrade) are capped to
// strict-origin-when-cross-origin; stricter policies stay. Because the
// network service applies the policy on every hop, this also keeps
// redirects of an initially same-origin request reduced.
net::ReferrerPolicy CapReferrerPolicyForStrictMode(net::ReferrerPolicy policy);

// Third party: not the main frame and not first-party to the request's
// site_for_cookies.
bool IsThirdPartyForStrictMode(const network::ResourceRequest& request,
                               bool is_main_frame);

// Sets `Sec-GPC: 1`, removes high-entropy UA client hints from third-party
// requests, reduces a cross-origin referrer to its origin (policy ORIGIN) and
// caps the referrer policy otherwise. Tracking parameters are the caller's
// concern: the contract strips them from main-frame navigations only.
void ApplyStrictRequestRules(network::ResourceRequest& request,
                             bool is_main_frame);

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_PRIVACY_STRICT_REQUEST_RULES_H_
