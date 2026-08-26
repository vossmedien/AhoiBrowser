// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_main_world_executor.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_document_actions.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace ahoi {
namespace {

const char kAhoiUserAgentMarkerKey = 0;

class AhoiUserAgentMarker final : public base::SupportsUserData::Data {};

bool IsEligibleNavigationContext(content::WebContents* web_contents,
                                 PrefService* prefs) {
  if (!web_contents || !prefs || web_contents->IsBeingDestroyed()) {
    return false;
  }
  content::BrowserContext* const browser_context =
      web_contents->GetBrowserContext();
  return browser_context && !browser_context->IsOffTheRecord() &&
         user_prefs::UserPrefs::IsInitialized(browser_context) &&
         user_prefs::UserPrefs::Get(browser_context) == prefs;
}

std::optional<DeveloperProfile> MaterializeHeaderSecretsOnWorker(
    DeveloperSecretStoreFactory secret_store_factory,
    DeveloperProfile profile) {
  if (secret_store_factory.is_null()) {
    return std::nullopt;
  }
  std::unique_ptr<DeveloperSecretStore> secret_store =
      secret_store_factory.Run();
  if (!secret_store) {
    return std::nullopt;
  }
  return MaterializeDeveloperProfileHeaderSecrets(std::move(profile),
                                                  *secret_store);
}

std::string_view CssForAsset(const DeveloperAsset& asset) {
  if (asset.style_language == DeveloperStyleLanguage::kCss) {
    return asset.source;
  }
  if (asset.compiled_style_version == kDeveloperStyleCompilerVersion) {
    return asset.compiled_css;
  }
  return std::string_view();
}

std::u16string BuildCssApplicationScript(const DeveloperAsset& asset) {
  std::string css_literal;
  std::string id_literal;
  const std::string_view css = CssForAsset(asset);
  if (css.empty() ||
      !base::EscapeJSONString(css, /*put_in_quotes=*/true, &css_literal) ||
      !base::EscapeJSONString(asset.id, /*put_in_quotes=*/true, &id_literal)) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(base::StrCat(
      {"(() => { const key = ", id_literal,
       "; const id = '__ahoi_asset_style__' + key; let style = "
       "document.getElementById(id); if (!style) { style = "
       "document.createElement('style'); style.id = id; "
       "style.setAttribute('data-ahoi-asset-style', key); "
       "(document.head || document.documentElement).appendChild(style); } "
       "style.textContent = ",
       css_literal, "; })();"}));
}

std::u16string BuildProfileCleanupScript(
    const std::vector<DeveloperAsset>& assets) {
  std::string ids = "[";
  bool first = true;
  for (const DeveloperAsset& asset : assets) {
    if (asset.kind != DeveloperAssetKind::kStyle || !asset.enabled ||
        CssForAsset(asset).empty()) {
      continue;
    }
    std::string literal;
    if (!base::EscapeJSONString(asset.id, /*put_in_quotes=*/true, &literal)) {
      return std::u16string();
    }
    if (!first) {
      ids.push_back(',');
    }
    first = false;
    ids.append(literal);
  }
  ids.push_back(']');
  return base::UTF8ToUTF16(base::StrCat(
      {"(() => { const active = new Set(", ids,
       "); document.querySelectorAll('[data-ahoi-asset-style]').forEach("
       "(node) => { if (!active.has(node.getAttribute("
       "'data-ahoi-asset-style'))) node.remove(); }); })();"}));
}

}  // namespace

bool ApplyDeveloperProfileToCurrentDocument(content::WebContents& web_contents,
                                            const DeveloperProfile& profile) {
  return ApplyDeveloperAssetsToCurrentDocument(web_contents, profile.assets);
}

bool ApplyDeveloperAssetsToCurrentDocument(
    content::WebContents& web_contents,
    const std::vector<DeveloperAsset>& assets) {
  content::RenderFrameHost* const frame = web_contents.GetPrimaryMainFrame();
  if (!frame || !web_contents.GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  const std::u16string cleanup_script = BuildProfileCleanupScript(assets);
  if (cleanup_script.empty()) {
    return false;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(cleanup_script, base::DoNothing(),
                                          kDeveloperToolkitIsolatedWorldId);
  bool all_applied = true;
  for (const DeveloperAsset& asset : assets) {
    if (!asset.enabled) {
      continue;
    }
    if (asset.kind == DeveloperAssetKind::kStyle) {
      const std::u16string css_script = BuildCssApplicationScript(asset);
      if (css_script.empty()) {
        all_applied = false;
        continue;
      }
      frame->ExecuteJavaScriptInIsolatedWorld(css_script, base::DoNothing(),
                                              kDeveloperToolkitIsolatedWorldId);
      continue;
    }
    if (asset.javascript_world == DeveloperJavaScriptWorld::kMain) {
      all_applied =
          ExecuteDeveloperJavaScriptInMainWorld(&web_contents, asset.source) &&
          all_applied;
      continue;
    }
    frame->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(asset.source),
                                            base::DoNothing(),
                                            kDeveloperToolkitIsolatedWorldId);
  }
  return all_applied;
}

void ApplyAhoiUserAgentOverride(content::WebContents& web_contents,
                                const DeveloperProfile* profile) {
  if (profile && profile->user_agent_enabled && !profile->user_agent.empty()) {
    web_contents.SetUserAgentOverride(
        blink::UserAgentOverride::UserAgentOnly(profile->user_agent), false);
    web_contents.SetUserData(&kAhoiUserAgentMarkerKey,
                             std::make_unique<AhoiUserAgentMarker>());
    return;
  }
  if (web_contents.GetUserData(&kAhoiUserAgentMarkerKey)) {
    web_contents.SetUserAgentOverride(blink::UserAgentOverride(), false);
    web_contents.RemoveUserData(&kAhoiUserAgentMarkerKey);
  }
}

DeveloperProfileTabHelper::DeveloperProfileTabHelper(
    content::WebContents* web_contents,
    PrefService* prefs)
    : content::WebContentsObserver(web_contents),
      store_(prefs, /*is_off_the_record=*/false) {}

DeveloperProfileTabHelper::~DeveloperProfileTabHelper() = default;

void DeveloperProfileTabHelper::SetWebContents(
    content::WebContents* web_contents) {
  Observe(web_contents);
}

void DeveloperProfileTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle || !navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    return;
  }
  const std::optional<DeveloperProfile> profile =
      GetDeveloperProfileForNavigation(store_, navigation_handle->GetURL());
  const std::vector<DeveloperAsset> assets =
      GetDeveloperAssetsForNavigation(store_, navigation_handle->GetURL());
  ClearDeveloperProfileNavigationRequest(*web_contents(),
                                         navigation_handle->GetNavigationId());
  UpdateDeveloperProfileNetworkState(*web_contents(),
                                     navigation_handle->GetURL(), profile);
  if (!assets.empty()) {
    ApplyDeveloperAssetsToCurrentDocument(*web_contents(), assets);
  }
}

// static
void DeveloperProfileNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::WebContents* const web_contents =
      registry.GetNavigationHandle().GetWebContents();
  content::BrowserContext* const browser_context =
      web_contents ? web_contents->GetBrowserContext() : nullptr;
  if (!browser_context || browser_context->IsOffTheRecord()) {
    return;
  }
  if (!user_prefs::UserPrefs::IsInitialized(browser_context)) {
    return;
  }
  PrefService* const prefs = user_prefs::UserPrefs::Get(browser_context);
  if (!prefs || prefs->GetDict(kDeveloperProfilesPref).empty()) {
    return;
  }
  registry.AddThrottle(
      std::make_unique<DeveloperProfileNavigationThrottle>(registry, prefs));
}

DeveloperProfileNavigationThrottle::DeveloperProfileNavigationThrottle(
    content::NavigationThrottleRegistry& registry,
    PrefService* prefs)
    : DeveloperProfileNavigationThrottle(
          registry,
          prefs,
          base::BindRepeating(&CreatePlatformDeveloperSecretStore)) {}

DeveloperProfileNavigationThrottle::DeveloperProfileNavigationThrottle(
    content::NavigationThrottleRegistry& registry,
    PrefService* prefs,
    DeveloperSecretStoreFactory secret_store_factory)
    : content::NavigationThrottle(registry),
      prefs_(prefs),
      store_(prefs, /*is_off_the_record=*/false),
      secret_store_factory_(std::move(secret_store_factory)),
      web_contents_(
          registry.GetNavigationHandle().GetWebContents()
              ? registry.GetNavigationHandle().GetWebContents()->GetWeakPtr()
              : base::WeakPtr<content::WebContents>()),
      navigation_id_(registry.GetNavigationHandle().GetNavigationId()) {}

DeveloperProfileNavigationThrottle::~DeveloperProfileNavigationThrottle() {
  if (web_contents_ && !web_contents_->IsBeingDestroyed()) {
    ClearDeveloperProfileNavigationRequest(*web_contents_, navigation_id_);
  }
}

content::NavigationThrottle::ThrottleCheckResult
DeveloperProfileNavigationThrottle::WillStartRequest() {
  return ApplyInitialRequestOverrides();
}

content::NavigationThrottle::ThrottleCheckResult
DeveloperProfileNavigationThrottle::WillRedirectRequest() {
  if (!navigation_handle()->IsInPrimaryMainFrame() || !web_contents_ ||
      navigation_handle()->GetWebContents() != web_contents_.get() ||
      !IsEligibleNavigationContext(web_contents_.get(), prefs_)) {
    if (web_contents_ && !web_contents_->IsBeingDestroyed()) {
      ClearDeveloperProfileNavigationRequest(*web_contents_, navigation_id_);
    }
    return content::NavigationThrottle::PROCEED;
  }
  const std::optional<DeveloperProfile> profile =
      GetDeveloperProfileForNavigation(store_, navigation_handle()->GetURL());
  ApplyAhoiUserAgentOverride(*web_contents_, profile ? &*profile : nullptr);
  navigation_handle()->SetIsOverridingUserAgent(profile &&
                                                profile->user_agent_enabled);
  RetargetDeveloperProfileNavigationRequest(*web_contents_, navigation_id_,
                                            navigation_handle()->GetURL());
  return content::NavigationThrottle::PROCEED;
}

const char* DeveloperProfileNavigationThrottle::GetNameForLogging() {
  return "AhoiDeveloperProfileNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
DeveloperProfileNavigationThrottle::ApplyInitialRequestOverrides() {
  if (!navigation_handle()->IsInPrimaryMainFrame() || !web_contents_ ||
      navigation_handle()->GetWebContents() != web_contents_.get() ||
      !IsEligibleNavigationContext(web_contents_.get(), prefs_)) {
    if (web_contents_ && !web_contents_->IsBeingDestroyed()) {
      ClearDeveloperProfileNavigationRequest(*web_contents_, navigation_id_);
    }
    return content::NavigationThrottle::PROCEED;
  }
  ClearDeveloperProfileNavigationRequest(*web_contents_);
  const std::optional<DeveloperProfile> profile =
      GetDeveloperProfileForNavigation(store_, navigation_handle()->GetURL());
  ApplyAhoiUserAgentOverride(*web_contents_, profile ? &*profile : nullptr);
  navigation_handle()->SetIsOverridingUserAgent(profile &&
                                                profile->user_agent_enabled);
  if (!profile) {
    return content::NavigationThrottle::PROCEED;
  }

  DeveloperProfile source_profile =
      MakeDeveloperProfileNetworkSnapshot(*profile);
  if (!DeveloperProfileHasActiveHeaderSecretReferences(source_profile)) {
    return content::NavigationThrottle::PROCEED;
  }
  const GURL request_url = navigation_handle()->GetURL();
  const url::Origin origin = url::Origin::Create(request_url);
  if (origin.opaque() || !request_url.SchemeIsHTTPOrHTTPS()) {
    return content::NavigationThrottle::PROCEED;
  }
  const bool posted = base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&MaterializeHeaderSecretsOnWorker, secret_store_factory_,
                     source_profile),
      base::BindOnce(
          &DeveloperProfileNavigationThrottle::OnHeaderSecretsMaterialized,
          weak_factory_.GetWeakPtr(), navigation_id_, request_url, origin,
          std::move(source_profile)));
  return posted ? content::NavigationThrottle::DEFER
                : content::NavigationThrottle::PROCEED;
}

void DeveloperProfileNavigationThrottle::OnHeaderSecretsMaterialized(
    int64_t navigation_id,
    GURL request_url,
    url::Origin origin,
    DeveloperProfile source_profile,
    std::optional<DeveloperProfile> materialized_profile) {
  bool valid = web_contents_ && !web_contents_->IsBeingDestroyed() &&
               navigation_id == navigation_id_ &&
               navigation_handle()->GetNavigationId() == navigation_id &&
               navigation_handle()->GetWebContents() == web_contents_.get() &&
               navigation_handle()->IsInPrimaryMainFrame() &&
               navigation_handle()->GetURL() == request_url &&
               url::Origin::Create(navigation_handle()->GetURL()) == origin;
  valid = valid && IsEligibleNavigationContext(web_contents_.get(), prefs_);
  if (!valid || !materialized_profile ||
      !StageDeveloperProfileNavigationRequest(
          *web_contents_, navigation_id, request_url, std::move(source_profile),
          std::move(*materialized_profile))) {
    if (web_contents_ && !web_contents_->IsBeingDestroyed()) {
      ClearDeveloperProfileNavigationRequest(*web_contents_, navigation_id);
    }
  }
  Resume();
}

}  // namespace ahoi
