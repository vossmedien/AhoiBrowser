// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "base/functional/bind.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

const char kDeveloperProfileNetworkStateKey = 0;
const char kDeveloperProfileNavigationRequestKey = 0;

class DeveloperProfileNetworkState final : public base::SupportsUserData::Data {
 public:
  DeveloperProfileNetworkState(url::Origin origin, DeveloperProfile profile)
      : origin(std::move(origin)), profile(std::move(profile)) {}

  const url::Origin origin;
  const DeveloperProfile profile;
};

class DeveloperProfileNavigationRequestState final
    : public base::SupportsUserData::Data {
 public:
  DeveloperProfileNavigationRequestState(int64_t navigation_id,
                                         GURL request_url,
                                         DeveloperProfile source_profile,
                                         DeveloperProfile materialized_profile)
      : navigation_id(navigation_id),
        request_url(std::move(request_url)),
        origin(url::Origin::Create(this->request_url)),
        source_profile(std::move(source_profile)),
        materialized_profile(std::move(materialized_profile)) {}

  const int64_t navigation_id;
  GURL request_url;
  const url::Origin origin;
  const DeveloperProfile source_profile;
  const DeveloperProfile materialized_profile;
};

const DeveloperProfileNetworkState* GetNetworkState(
    content::WebContents* web_contents) {
  return web_contents
             ? static_cast<const DeveloperProfileNetworkState*>(
                   web_contents->GetUserData(&kDeveloperProfileNetworkStateKey))
             : nullptr;
}

DeveloperProfileNavigationRequestState* GetNavigationRequestState(
    content::WebContents* web_contents) {
  return web_contents ? static_cast<DeveloperProfileNavigationRequestState*>(
                            web_contents->GetUserData(
                                &kDeveloperProfileNavigationRequestKey))
                      : nullptr;
}

bool IsEligibleWebContents(content::WebContents* web_contents,
                           PrefService* prefs,
                           bool is_off_the_record) {
  if (!web_contents || !prefs || is_off_the_record ||
      web_contents->IsBeingDestroyed() ||
      !developer_toolkit_prefs::IsToolkitEnabled(*prefs)) {
    return false;
  }
  content::BrowserContext* const browser_context =
      web_contents->GetBrowserContext();
  return browser_context && !browser_context->IsOffTheRecord() &&
         user_prefs::UserPrefs::IsInitialized(browser_context) &&
         user_prefs::UserPrefs::Get(browser_context) == prefs;
}

DeveloperProfile DisableUnresolvedHeaderProfile(DeveloperProfile profile) {
  if (!DeveloperProfileHasActiveHeaderSecretReferences(profile)) {
    return profile;
  }
  profile.header_rules_enabled = false;
  profile.response_header_rules_enabled = false;
  profile.header_rules.clear();
  profile.response_header_rules.clear();
  return profile;
}

bool MaterializedRulesMatch(
    const std::vector<DeveloperHeaderRule>& source,
    const std::vector<DeveloperHeaderRule>& materialized,
    bool enabled) {
  if (!enabled) {
    return source == materialized;
  }
  if (source.size() != materialized.size()) {
    return false;
  }
  for (size_t index = 0; index < source.size(); ++index) {
    const DeveloperHeaderRule& before = source[index];
    const DeveloperHeaderRule& after = materialized[index];
    if (!IsValidDeveloperHeaderName(before.name) || before.name != after.name ||
        before.action != after.action || !after.secret_reference.empty()) {
      return false;
    }
    if (before.action == DeveloperHeaderAction::kRemove) {
      if (!before.value.empty() || !before.secret_reference.empty() ||
          !after.value.empty()) {
        return false;
      }
      continue;
    }
    if (before.action != DeveloperHeaderAction::kSet) {
      return false;
    }
    if (before.secret_reference.empty()) {
      if (before.value.empty() || !IsValidDeveloperHeaderValue(before.value) ||
          before.value != after.value) {
        return false;
      }
    } else if (!before.value.empty() ||
               !IsValidDeveloperSecretReference(before.secret_reference) ||
               after.value.empty() ||
               !IsValidDeveloperHeaderValue(after.value)) {
      return false;
    }
  }
  return true;
}

bool MaterializedProfileMatches(const DeveloperProfile& source,
                                const DeveloperProfile& materialized) {
  DeveloperProfile source_metadata = source;
  DeveloperProfile materialized_metadata = materialized;
  source_metadata.header_rules.clear();
  source_metadata.response_header_rules.clear();
  materialized_metadata.header_rules.clear();
  materialized_metadata.response_header_rules.clear();
  return source_metadata == materialized_metadata &&
         MaterializedRulesMatch(source.header_rules, materialized.header_rules,
                                source.header_rules_enabled) &&
         MaterializedRulesMatch(source.response_header_rules,
                                materialized.response_header_rules,
                                source.response_header_rules_enabled);
}

}  // namespace

DeveloperProfile MakeDeveloperProfileNetworkSnapshot(DeveloperProfile profile) {
  profile.name.clear();
  profile.assets.clear();
  return profile;
}

std::optional<DeveloperProfile> GetDeveloperProfileNetworkSnapshotForRequest(
    const network::ResourceRequest& request,
    PrefService* prefs,
    bool is_off_the_record,
    content::WebContents* web_contents) {
  if (!request.url.SchemeIsHTTPOrHTTPS() ||
      !IsEligibleWebContents(web_contents, prefs, is_off_the_record)) {
    return std::nullopt;
  }
  const url::Origin origin = url::Origin::Create(request.url);
  if (origin.opaque()) {
    return std::nullopt;
  }
  std::optional<DeveloperProfile> profile;
  if (request.is_outermost_main_frame) {
    if (request.navigation_redirect_chain.empty() ||
        request.navigation_redirect_chain.back() != request.url) {
      return std::nullopt;
    }
    profile = GetDeveloperNetworkProfileForTab(prefs, web_contents, request.url);
    if (profile) {
      profile = MakeDeveloperProfileNetworkSnapshot(std::move(*profile));
    }
    if (profile && DeveloperProfileHasActiveHeaderSecretReferences(*profile)) {
      const DeveloperProfileNavigationRequestState* const state =
          GetNavigationRequestState(web_contents);
      if (state && state->request_url == request.url &&
          state->origin == origin && state->source_profile == *profile) {
        profile = state->materialized_profile;
      }
    }
  } else if (const DeveloperProfileNetworkState* state =
                 GetNetworkState(web_contents);
             state) {
    if (!state->profile.headers_persistent) {
      const auto current = GetDeveloperNetworkProfileForTab(
          prefs, web_contents, web_contents->GetLastCommittedURL());
      if (!current || MakeDeveloperProfileNetworkSnapshot(*current) !=
                          state->profile) {
        return std::nullopt;
      }
    }
    if (state->origin == origin) {
      profile = state->profile;
    } else if (state->profile.cache_disabled) {
      // Cache-off belongs to the owning document, including its CDN fetches.
      // Header/UA rules and secret references retain their exact-origin scope.
      profile = DeveloperProfile{.cache_disabled = true};
    }
  }
  return profile;
}

std::unique_ptr<blink::URLLoaderThrottle>
MaybeCreateDeveloperProfileURLLoaderThrottle(
    const network::ResourceRequest& request,
    PrefService* prefs,
    bool is_off_the_record,
    content::WebContents* web_contents) {
  auto profile = GetDeveloperProfileNetworkSnapshotForRequest(
      request, prefs, is_off_the_record, web_contents);
  if (profile) {
    profile = DisableUnresolvedHeaderProfile(std::move(*profile));
  }
  if (!profile ||
      (!profile->user_agent_enabled && !profile->header_rules_enabled &&
       !profile->response_header_rules_enabled && !profile->cache_disabled)) {
    return nullptr;
  }
  base::RepeatingCallback<bool()> approval;
  if (request.is_outermost_main_frame) {
    const auto source = GetDeveloperNetworkProfileForTab(prefs, web_contents, request.url);
    if (!source) {
      return nullptr;
    }
    const auto* helper = DeveloperProfileTabHelper::FromWebContents(web_contents);
    const auto* staged = GetNavigationRequestState(web_contents);
    const std::optional<int64_t> staged_id =
        staged ? std::optional<int64_t>(staged->navigation_id) : std::nullopt;
    approval = base::BindRepeating(
        [](base::WeakPtr<content::WebContents> contents,
           const PrefService* expected_prefs, GURL url,
           DeveloperProfile source_metadata, std::string owner,
           uint64_t generation, std::optional<int64_t> navigation_id) {
          if (!contents || contents->IsBeingDestroyed()) {
            return false;
          }
          auto* context = contents->GetBrowserContext();
          if (!context || !user_prefs::UserPrefs::IsInitialized(context)) {
            return false;
          }
          auto* current_prefs = user_prefs::UserPrefs::Get(context);
          if (current_prefs != expected_prefs ||
              !IsEligibleWebContents(contents.get(), current_prefs, false)) {
            return false;
          }
          const auto* current_helper =
              DeveloperProfileTabHelper::FromWebContents(contents.get());
          if ((current_helper ? current_helper->tab_token() : std::string()) !=
                  owner ||
              (current_helper ? current_helper->activation_generation() : 0) !=
                  generation) {
            return false;
          }
          const auto current =
              GetDeveloperNetworkProfileForTab(current_prefs, contents.get(), url);
          if (!current || MakeDeveloperProfileNetworkSnapshot(*current) !=
                              source_metadata) {
            return false;
          }
          if (navigation_id) {
            const auto* state = GetNavigationRequestState(contents.get());
            return state && state->navigation_id == *navigation_id &&
                   state->source_profile == source_metadata;
          }
          return true;
        }, web_contents->GetWeakPtr(), prefs, request.url,
        MakeDeveloperProfileNetworkSnapshot(*source),
        helper ? helper->tab_token() : std::string(),
        helper ? helper->activation_generation() : 0, staged_id);
  }
  return std::make_unique<DeveloperProfileURLLoaderThrottle>(
      url::Origin::Create(request.url), std::move(*profile), std::move(approval));
}

void UpdateDeveloperProfileNetworkState(
    content::WebContents& web_contents,
    const GURL& committed_url,
    const std::optional<DeveloperProfile>& profile) {
  if (!profile || !committed_url.SchemeIsHTTPOrHTTPS()) {
    web_contents.RemoveUserData(&kDeveloperProfileNetworkStateKey);
    return;
  }
  web_contents.SetUserData(&kDeveloperProfileNetworkStateKey,
                           std::make_unique<DeveloperProfileNetworkState>(
                               url::Origin::Create(committed_url),
                               MakeDeveloperProfileNetworkSnapshot(*profile)));
}

bool StageDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    int64_t navigation_id,
    const GURL& request_url,
    DeveloperProfile source_profile,
    DeveloperProfile materialized_profile) {
  const url::Origin origin = url::Origin::Create(request_url);
  if (web_contents.IsBeingDestroyed()) {
    return false;
  }
  if (!request_url.SchemeIsHTTPOrHTTPS() || origin.opaque() ||
      !DeveloperProfileHasActiveHeaderSecretReferences(source_profile) ||
      DeveloperProfileHasActiveHeaderSecretReferences(materialized_profile) ||
      !MaterializedProfileMatches(source_profile, materialized_profile)) {
    ClearDeveloperProfileNavigationRequest(web_contents, navigation_id);
    return false;
  }
  web_contents.SetUserData(
      &kDeveloperProfileNavigationRequestKey,
      std::make_unique<DeveloperProfileNavigationRequestState>(
          navigation_id, request_url,
          MakeDeveloperProfileNetworkSnapshot(std::move(source_profile)),
          MakeDeveloperProfileNetworkSnapshot(
              std::move(materialized_profile))));
  return true;
}

bool RetargetDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    int64_t navigation_id,
    const GURL& request_url) {
  DeveloperProfileNavigationRequestState* const state =
      GetNavigationRequestState(&web_contents);
  if (!state || state->navigation_id != navigation_id ||
      !request_url.SchemeIsHTTPOrHTTPS() ||
      url::Origin::Create(request_url) != state->origin) {
    ClearDeveloperProfileNavigationRequest(web_contents, navigation_id);
    return false;
  }
  state->request_url = request_url;
  return true;
}

void ClearDeveloperProfileNavigationRequest(
    content::WebContents& web_contents,
    std::optional<int64_t> navigation_id) {
  const DeveloperProfileNavigationRequestState* const state =
      GetNavigationRequestState(&web_contents);
  if (!state || (navigation_id && state->navigation_id != *navigation_id)) {
    return;
  }
  web_contents.RemoveUserData(&kDeveloperProfileNavigationRequestKey);
}

DeveloperProfileURLLoaderThrottle::DeveloperProfileURLLoaderThrottle(
    url::Origin origin,
    DeveloperProfile profile,
    base::RepeatingCallback<bool()> approval)
    : origin_(std::move(origin)),
      profile_(DisableUnresolvedHeaderProfile(
          MakeDeveloperProfileNetworkSnapshot(std::move(profile)))),
      approval_(std::move(approval)) {}

DeveloperProfileURLLoaderThrottle::~DeveloperProfileURLLoaderThrottle() =
    default;

bool DeveloperProfileURLLoaderThrottle::ValidateApproval() {
  if (revoked_) {
    return false;
  }
  if (approval_ && !approval_.Run()) {
    revoked_ = true;
    profile_ = DeveloperProfile();
    approval_.Reset();
    return false;
  }
  return true;
}

void DeveloperProfileURLLoaderThrottle::RestoreOriginalHeadersForRedirect(
    network::HttpRequestHeadersUpdateParams& headers_update_params) const {
  RestoreForRedirect(headers_update_params);
}

void DeveloperProfileURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* /*defer*/) {
  if (!request || !ValidateApproval() ||
      url::Origin::Create(request->url) != origin_) {
    return;
  }
  if (profile_.cache_disabled) {
    request->load_flags |= net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
  }
  ApplyToHeaders(request->headers);
}

const char*
DeveloperProfileURLLoaderThrottle::NameForLoggingWillStartRequest() {
  return "AhoiDeveloperProfileURLLoaderThrottle";
}

void DeveloperProfileURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& /*response_head*/,
    bool* /*defer*/,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  if (!redirect_info || !headers_update_params) {
    return;
  }
  if (!ValidateApproval()) {
    RestoreForRedirect(*headers_update_params);
    return;
  }
  if (url::Origin::Create(redirect_info->new_url) == origin_) {
    ApplyForRedirect(*headers_update_params);
  } else {
    RestoreForRedirect(*headers_update_params);
  }
}

void DeveloperProfileURLLoaderThrottle::WillProcessResponse(
    const GURL& response_url,
    network::mojom::URLResponseHead* response_head,
    bool* /*defer*/) {
  if (!ValidateApproval() || !response_head || !response_head->headers ||
      url::Origin::Create(response_url) != origin_ ||
      !profile_.response_header_rules_enabled) {
    return;
  }
  ApplyToResponseHeaders(*response_head->headers);
}

const char*
DeveloperProfileURLLoaderThrottle::NameForLoggingWillProcessResponse() {
  return "AhoiDeveloperProfileURLLoaderThrottle";
}

void DeveloperProfileURLLoaderThrottle::RememberOriginalHeader(
    const net::HttpRequestHeaders& headers,
    std::string_view name) {
  for (const OriginalHeader& header : original_headers_) {
    if (base::EqualsCaseInsensitiveASCII(header.name, name)) {
      return;
    }
  }
  original_headers_.push_back(
      {.name = std::string(name), .value = headers.GetHeader(name)});
}

void DeveloperProfileURLLoaderThrottle::ApplyToHeaders(
    net::HttpRequestHeaders& headers) {
  if (profile_.user_agent_enabled) {
    RememberOriginalHeader(headers, net::HttpRequestHeaders::kUserAgent);
    headers.SetHeader(net::HttpRequestHeaders::kUserAgent, profile_.user_agent);
  }
  if (!profile_.header_rules_enabled) {
    return;
  }
  for (const DeveloperHeaderRule& rule : profile_.header_rules) {
    if (!rule.secret_reference.empty()) {
      continue;
    }
    RememberOriginalHeader(headers, rule.name);
    if (rule.action == DeveloperHeaderAction::kSet) {
      headers.SetHeader(rule.name, rule.value);
    } else {
      headers.RemoveHeader(rule.name);
    }
  }
}

void DeveloperProfileURLLoaderThrottle::RestoreForRedirect(
    network::HttpRequestHeadersUpdateParams& headers_update_params) const {
  for (const OriginalHeader& header : original_headers_) {
    headers_update_params.modified_headers.RemoveHeader(header.name);
    headers_update_params.modified_cors_exempt_headers.RemoveHeader(header.name);
    headers_update_params.removed_headers.push_back(header.name);
    if (header.value) {
      headers_update_params.modified_headers.SetHeader(header.name,
                                                       *header.value);
    }
  }
}

void DeveloperProfileURLLoaderThrottle::ApplyForRedirect(
    network::HttpRequestHeadersUpdateParams& headers_update_params) const {
  if (profile_.user_agent_enabled) {
    headers_update_params.modified_headers.SetHeader(
        net::HttpRequestHeaders::kUserAgent, profile_.user_agent);
  }
  if (!profile_.header_rules_enabled) {
    return;
  }
  for (const DeveloperHeaderRule& rule : profile_.header_rules) {
    if (!rule.secret_reference.empty()) {
      continue;
    }
    if (rule.action == DeveloperHeaderAction::kSet) {
      headers_update_params.modified_headers.SetHeader(rule.name, rule.value);
    } else {
      headers_update_params.removed_headers.push_back(rule.name);
    }
  }
}

void DeveloperProfileURLLoaderThrottle::ApplyToResponseHeaders(
    net::HttpResponseHeaders& headers) const {
  for (const DeveloperHeaderRule& rule : profile_.response_header_rules) {
    if (!rule.secret_reference.empty()) {
      continue;
    }
    if (rule.action == DeveloperHeaderAction::kSet) {
      headers.SetHeader(rule.name, rule.value);
    } else {
      headers.RemoveHeader(rule.name);
    }
  }
}

}  // namespace ahoi
