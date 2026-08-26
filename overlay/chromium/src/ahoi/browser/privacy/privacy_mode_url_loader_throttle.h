// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_THROTTLE_H_
#define AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_THROTTLE_H_

#include <memory>

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "net/cookies/site_for_cookies.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

class PrefService;

namespace network {
struct ResourceRequest;
}  // namespace network

namespace ahoi::privacy {

std::unique_ptr<blink::URLLoaderThrottle>
MaybeCreatePrivacyModeURLLoaderThrottle(const network::ResourceRequest& request,
                                        const PrefService* prefs,
                                        bool is_off_the_record);

class PrivacyModeURLLoaderThrottle final : public blink::URLLoaderThrottle {
 public:
  PrivacyModeURLLoaderThrottle(PrivacyPolicy policy,
                               GURL policy_origin,
                               net::SiteForCookies site_for_cookies,
                               bool is_main_frame);
  PrivacyModeURLLoaderThrottle(const PrivacyModeURLLoaderThrottle&) = delete;
  PrivacyModeURLLoaderThrottle& operator=(const PrivacyModeURLLoaderThrottle&) =
      delete;
  ~PrivacyModeURLLoaderThrottle() override;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;
  const char* NameForLoggingWillStartRequest() override;

 private:
  void ApplyToRequest(network::ResourceRequest& request) const;
  bool IsThirdPartyRequest(const GURL& url) const;

  const PrivacyPolicy policy_;
  const GURL policy_origin_;
  const net::SiteForCookies site_for_cookies_;
  const bool is_main_frame_;
};

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_PRIVACY_MODE_URL_LOADER_THROTTLE_H_
