// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_main_world_executor.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_document_actions.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
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

const char kDeveloperProfileTabHelperKey = 0;

class DeveloperProfileTabHelperMarker final
    : public base::SupportsUserData::Data {
 public:
  explicit DeveloperProfileTabHelperMarker(
      base::WeakPtr<DeveloperProfileTabHelper> helper)
      : helper(std::move(helper)) {}

  const base::WeakPtr<DeveloperProfileTabHelper> helper;
};

bool IsEligibleNavigationContext(content::WebContents* web_contents,
                                 PrefService* prefs) {
  if (!web_contents || !prefs || web_contents->IsBeingDestroyed() ||
      !developer_toolkit_prefs::IsToolkitEnabled(*prefs)) {
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

std::string_view RuntimeIdForAsset(const DeveloperAsset& asset) {
  return asset.runtime_id.empty() ? std::string_view(asset.id)
                                  : std::string_view(asset.runtime_id);
}

std::u16string BuildCssApplicationScript(const DeveloperAsset& asset) {
  std::string css_literal;
  std::string id_literal;
  const std::string_view css = CssForAsset(asset);
  if (css.empty() ||
      !base::EscapeJSONString(css, /*put_in_quotes=*/true, &css_literal) ||
      !base::EscapeJSONString(RuntimeIdForAsset(asset),
                              /*put_in_quotes=*/true, &id_literal)) {
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
    if (!base::EscapeJSONString(RuntimeIdForAsset(asset),
                                /*put_in_quotes=*/true, &literal)) {
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
  auto* context = web_contents.GetBrowserContext();
  if (!context || !user_prefs::UserPrefs::IsInitialized(context) ||
      !IsEligibleNavigationContext(
          &web_contents, user_prefs::UserPrefs::Get(context))) {
    return false;
  }
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

DeveloperProfileTabHelper::DeveloperProfileTabHelper(
    content::WebContents* web_contents,
    PrefService* prefs)
    : content::WebContentsObserver(web_contents),
      prefs_(prefs),
      store_(prefs, /*is_off_the_record=*/false),
      tab_token_(base::Uuid::GenerateRandomV4().AsLowercaseString()) {
  AttachToWebContents(web_contents);
  InitializeActivationObserver();
}

DeveloperProfileTabHelper::~DeveloperProfileTabHelper() {
  DetachFromWebContents(web_contents());
}

// static
DeveloperProfileTabHelper* DeveloperProfileTabHelper::FromWebContents(
    content::WebContents* web_contents) {
  auto* marker =
      web_contents
          ? static_cast<DeveloperProfileTabHelperMarker*>(
                web_contents->GetUserData(&kDeveloperProfileTabHelperKey))
          : nullptr;
  return marker ? marker->helper.get() : nullptr;
}

void DeveloperProfileTabHelper::SetWebContents(
    content::WebContents* web_contents) {
  if (web_contents == this->web_contents()) {
    return;
  }
  DetachFromWebContents(this->web_contents());
  cache_disabled_for_tab_ = false;
  ++activation_generation_;
  Observe(web_contents);
  AttachToWebContents(web_contents);
  InitializeActivationObserver();
}

void DeveloperProfileTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  // Same scope as DeveloperProfileNavigationThrottle::MaybeCreateAndAdd:
  // without saved profiles nothing is touched, so other user-agent
  // overrides keep working.
  if (!navigation_handle || !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument() ||
      navigation_handle->GetWebContents() != web_contents() ||
      !IsEligibleNavigationContext(web_contents(), prefs_) ||
      (prefs_->GetDict(kDeveloperProfilesPref).empty() &&
       !HasTemporaryHeaders() && !IsCacheDisabledForCurrentTab())) {
    if (HasBoundContext() && navigation_handle &&
        navigation_handle->IsInPrimaryMainFrame() &&
        !navigation_handle->IsSameDocument() && web_contents() &&
        navigation_handle->GetWebContents() == web_contents()) {
      const bool owned = HasAhoiUserAgentOverride(*web_contents());
      ApplyAhoiUserAgentOverride(*web_contents(), nullptr);
      if (owned) {
        navigation_handle->SetIsOverridingUserAgent(false);
      }
    }
    return;
  }
  const std::optional<DeveloperProfile> profile =
      GetDeveloperNetworkProfileForTab(prefs_, web_contents(), navigation_handle->GetURL());
  const bool was_owned = HasAhoiUserAgentOverride(*web_contents());
  ApplyAhoiUserAgentOverride(*web_contents(), profile ? &*profile : nullptr);
  if (profile && profile->user_agent_enabled) {
    navigation_handle->SetIsOverridingUserAgent(true);
  } else if (was_owned) {
    navigation_handle->SetIsOverridingUserAgent(false);
  }
}

void DeveloperProfileTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle || !navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument() ||
      navigation_handle->GetWebContents() != web_contents()) {
    return;
  }
  if (!IsEligibleNavigationContext(web_contents(), prefs_)) {
    active_assets_.clear();
    if (HasBoundContext()) {
      ClearDeveloperProfileNavigationRequest(*web_contents());
      UpdateDeveloperProfileNetworkState(*web_contents(),
                                         navigation_handle->GetURL(),
                                         std::nullopt);
      ApplyAhoiUserAgentOverride(*web_contents(), nullptr);
    }
    return;
  }
  const std::optional<DeveloperProfile> profile =
      GetDeveloperNetworkProfileForTab(prefs_, web_contents(), navigation_handle->GetURL());
  const std::vector<DeveloperAsset> assets =
      TakeAssetsForNavigation(navigation_handle->GetURL());
  ClearDeveloperProfileNavigationRequest(*web_contents(),
                                         navigation_handle->GetNavigationId());
  UpdateDeveloperProfileNetworkState(*web_contents(),
                                     navigation_handle->GetURL(), profile);
  if (!assets.empty()) {
    ApplyDeveloperAssetsToCurrentDocument(*web_contents(), assets);
  }
}

void DeveloperProfileTabHelper::AttachToWebContents(
    content::WebContents* web_contents) {
  if (!web_contents || web_contents->IsBeingDestroyed() || !HasBoundContext()) {
    return;
  }
  web_contents->SetUserData(&kDeveloperProfileTabHelperKey,
                            std::make_unique<DeveloperProfileTabHelperMarker>(
                                weak_factory_.GetWeakPtr()));
}

void DeveloperProfileTabHelper::DetachFromWebContents(
    content::WebContents* web_contents) {
  if (!web_contents || web_contents->IsBeingDestroyed() ||
      FromWebContents(web_contents) != this) {
    return;
  }
  web_contents->RemoveUserData(&kDeveloperProfileTabHelperKey);
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
  const auto* helper = DeveloperProfileTabHelper::FromWebContents(web_contents);
  if (!prefs || !developer_toolkit_prefs::IsToolkitEnabled(*prefs) ||
      (prefs->GetDict(kDeveloperProfilesPref).empty() &&
                 (!helper || (!helper->HasTemporaryHeaders() && !helper->IsCacheDisabledForCurrentTab())))) {
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
      navigation_id_(registry.GetNavigationHandle().GetNavigationId()),
      activation_generation_(DeveloperProfileTabHelper::FromWebContents(
          web_contents_.get()) ? DeveloperProfileTabHelper::FromWebContents(
          web_contents_.get())->activation_generation() : 0) {}

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
      GetDeveloperNetworkProfileForTab(prefs_, web_contents_.get(),
                                navigation_handle()->GetURL());
  // A redirect cannot change the navigation's user-agent flag any more
  // (only DidStartNavigation may); the WebContents override still follows
  // the redirect target for the requests after it.
  ApplyAhoiUserAgentOverride(*web_contents_, profile ? &*profile : nullptr);
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
      GetDeveloperNetworkProfileForTab(prefs_, web_contents_.get(),
                                navigation_handle()->GetURL());
  // The user-agent decision is made in DeveloperProfileTabHelper::
  // DidStartNavigation; here it would trip NavigationRequest's CHECK.
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
  const auto* helper =
      DeveloperProfileTabHelper::FromWebContents(web_contents_.get());
  valid = valid && (helper ? helper->activation_generation() : 0) ==
                       activation_generation_;
  const auto current = valid ? GetDeveloperNetworkProfileForTab(
                                   prefs_, web_contents_.get(), request_url)
                             : std::nullopt;
  valid = valid && current &&
          MakeDeveloperProfileNetworkSnapshot(*current) == source_profile;
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
