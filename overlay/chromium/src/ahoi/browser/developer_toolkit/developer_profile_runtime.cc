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

const char kAhoiUserAgentMarkerKey = 0;
const char kDeveloperProfileTabHelperKey = 0;

class AhoiUserAgentMarker final : public base::SupportsUserData::Data {};

class DeveloperProfileTabHelperMarker final
    : public base::SupportsUserData::Data {
 public:
  explicit DeveloperProfileTabHelperMarker(
      base::WeakPtr<DeveloperProfileTabHelper> helper)
      : helper(std::move(helper)) {}

  const base::WeakPtr<DeveloperProfileTabHelper> helper;
};

struct DeveloperProfileChange {
  url::Origin origin;
  std::optional<DeveloperProfile> before;
  std::optional<DeveloperProfile> after;
};

bool HasPersistentProfileData(const DeveloperProfile& profile) {
  return !profile.assets.empty() || profile.user_agent_enabled ||
         !profile.user_agent.empty() || profile.header_rules_enabled ||
         profile.header_rules_sync_enabled || !profile.header_rules.empty() ||
         profile.response_header_rules_enabled ||
         profile.response_header_rules_sync_enabled ||
         profile.response_header_advanced_mode_acknowledged ||
         !profile.response_header_rules.empty() || profile.cache_disabled;
}

bool CanReplaceProfile(const DeveloperProfileStore& store,
                       const url::Origin& origin,
                       const std::optional<DeveloperProfile>& profile) {
  if (!profile || store.Get(origin)) {
    return true;
  }
  return store.ListOrigins().size() < kMaxDeveloperProfiles;
}

bool ReplaceProfile(DeveloperProfileStore& store,
                    const url::Origin& origin,
                    const std::optional<DeveloperProfile>& profile) {
  if (profile) {
    return store.Set(origin, *profile);
  }
  return !store.Get(origin) || store.Remove(origin);
}

bool ApplyProfileChanges(DeveloperProfileStore& store,
                         const std::vector<DeveloperProfileChange>& changes) {
  size_t applied = 0;
  for (; applied < changes.size(); ++applied) {
    if (ReplaceProfile(store, changes[applied].origin,
                       changes[applied].after)) {
      continue;
    }
    while (applied > 0) {
      --applied;
      CHECK(ReplaceProfile(store, changes[applied].origin,
                           changes[applied].before));
    }
    return false;
  }
  return true;
}

bool RestoreProfileChanges(DeveloperProfileStore& store,
                           const std::vector<DeveloperProfileChange>& changes) {
  bool restored = true;
  for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
    restored = ReplaceProfile(store, it->origin, it->before) && restored;
  }
  return restored;
}

void DisablePersistentOverrides(DeveloperProfile* profile) {
  profile->user_agent_enabled = false;
  profile->header_rules_enabled = false;
  profile->response_header_rules_enabled = false;
  profile->cache_disabled = false;
}

bool BuildResetChanges(const DeveloperProfileStore& store,
                       const GURL& url,
                       std::string_view tab_token,
                       bool persistent,
                       std::vector<DeveloperProfileChange>* changes) {
  CHECK(changes);
  const url::Origin target_origin = url::Origin::Create(url);
  for (const url::Origin& owner_origin : store.ListOrigins()) {
    const std::optional<DeveloperProfile> before = store.Get(owner_origin);
    if (!before) {
      continue;
    }
    DeveloperProfile after = *before;
    const size_t old_asset_count = after.assets.size();
    std::erase_if(after.assets, [&](const DeveloperAsset& asset) {
      return DoesDeveloperAssetMatch(owner_origin, asset, url, tab_token);
    });
    bool changed = after.assets.size() != old_asset_count;
    if (persistent && owner_origin == target_origin) {
      const DeveloperProfile before_disabling = after;
      DisablePersistentOverrides(&after);
      changed = changed || after != before_disabling;
    }
    if (!changed) {
      continue;
    }

    std::optional<DeveloperProfile> replacement;
    if (persistent ? HasPersistentProfileData(after) : !after.assets.empty()) {
      replacement = std::move(after);
      const DeveloperProfileValidationError validation =
          persistent ? ValidateDeveloperProfileForPersistence(owner_origin,
                                                              *replacement)
                     : ValidateDeveloperProfile(owner_origin, *replacement);
      if (validation != DeveloperProfileValidationError::kNone) {
        return false;
      }
    }
    changes->push_back(
        {.origin = owner_origin, .before = before, .after = replacement});
  }
  return true;
}

void ClearTransientStore(InMemoryDeveloperProfileStore& store) {
  for (const url::Origin& origin : store.ListOrigins()) {
    store.Remove(origin);
  }
}

void AppendAssets(const std::optional<DeveloperProfile>& source,
                  DeveloperProfile* target) {
  if (!source || !target) {
    return;
  }
  if (target->name.empty()) {
    target->name = source->name;
  }
  target->assets.insert(target->assets.end(), source->assets.begin(),
                        source->assets.end());
}

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
      store_(prefs, /*is_off_the_record=*/false),
      tab_token_(base::Uuid::GenerateRandomV4().AsLowercaseString()) {
  AttachToWebContents(web_contents);
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
  Observe(web_contents);
  AttachToWebContents(web_contents);
}

bool DeveloperProfileTabHelper::SaveProfile(const url::Origin& origin,
                                            const DeveloperProfile& profile) {
  if (ValidateDeveloperProfile(origin, profile) !=
      DeveloperProfileValidationError::kNone) {
    return false;
  }

  DeveloperProfile persistent = profile;
  DeveloperProfile reload{.name = profile.name};
  DeveloperProfile once{.name = profile.name};
  persistent.assets.clear();
  for (const DeveloperAsset& asset : profile.assets) {
    if (asset.scope.kind == DeveloperAssetScopeKind::kCurrentTab &&
        asset.scope.value != tab_token_) {
      return false;
    }
    switch (asset.lifetime) {
      case DeveloperAssetLifetime::kOnce:
        once.assets.push_back(asset);
        break;
      case DeveloperAssetLifetime::kReload:
        reload.assets.push_back(asset);
        break;
      case DeveloperAssetLifetime::kRestart:
        persistent.assets.push_back(asset);
        break;
    }
  }
  if (ValidateDeveloperProfileForPersistence(origin, persistent) !=
          DeveloperProfileValidationError::kNone ||
      (!reload.assets.empty() && ValidateDeveloperProfile(origin, reload) !=
                                     DeveloperProfileValidationError::kNone) ||
      (!once.assets.empty() && ValidateDeveloperProfile(origin, once) !=
                                   DeveloperProfileValidationError::kNone)) {
    return false;
  }

  const std::optional<DeveloperProfile> old_persistent = store_.Get(origin);
  const std::optional<DeveloperProfile> old_reload = reload_store_.Get(origin);
  const std::optional<DeveloperProfile> old_once = once_store_.Get(origin);
  std::optional<DeveloperProfile> new_persistent;
  std::optional<DeveloperProfile> new_reload;
  std::optional<DeveloperProfile> new_once;
  if (HasPersistentProfileData(persistent)) {
    new_persistent = std::move(persistent);
  }
  if (!reload.assets.empty()) {
    new_reload = std::move(reload);
  }
  if (!once.assets.empty()) {
    new_once = std::move(once);
  }

  // Prefs can be changed by sync or another surface while this tab retains
  // transient state. Prove that every new entry fits before mutating any of
  // the three stores. Opaque secret references remain only in the persistent
  // snapshot; transient profiles contain name/assets exclusively.
  if (!CanReplaceProfile(store_, origin, new_persistent) ||
      !CanReplaceProfile(reload_store_, origin, new_reload) ||
      !CanReplaceProfile(once_store_, origin, new_once)) {
    return false;
  }
  if (!ReplaceProfile(reload_store_, origin, new_reload)) {
    return false;
  }
  if (!ReplaceProfile(once_store_, origin, new_once)) {
    CHECK(ReplaceProfile(reload_store_, origin, old_reload));
    return false;
  }
  // Commit PrefService last so pref observers can never see the new durable
  // profile paired with stale once/reload state. Any unexpected Pref failure
  // restores both transient snapshots before returning.
  if (!ReplaceProfile(store_, origin, new_persistent)) {
    CHECK(ReplaceProfile(once_store_, origin, old_once));
    CHECK(ReplaceProfile(reload_store_, origin, old_reload));
    return false;
  }
  return true;
}

bool DeveloperProfileTabHelper::RemoveProfile(const url::Origin& origin) {
  const std::optional<DeveloperProfile> old_persistent = store_.Get(origin);
  const std::optional<DeveloperProfile> old_reload = reload_store_.Get(origin);
  const std::optional<DeveloperProfile> old_once = once_store_.Get(origin);
  if (!old_persistent && !old_reload && !old_once) {
    return false;
  }
  if (!ReplaceProfile(reload_store_, origin, std::nullopt)) {
    return false;
  }
  if (!ReplaceProfile(once_store_, origin, std::nullopt)) {
    CHECK(ReplaceProfile(reload_store_, origin, old_reload));
    return false;
  }
  if (!ReplaceProfile(store_, origin, std::nullopt)) {
    CHECK(ReplaceProfile(once_store_, origin, old_once));
    CHECK(ReplaceProfile(reload_store_, origin, old_reload));
    return false;
  }
  return true;
}

std::optional<DeveloperProfile> DeveloperProfileTabHelper::GetProfile(
    const url::Origin& origin) const {
  std::optional<DeveloperProfile> result = store_.Get(origin);
  DeveloperProfile merged;
  if (result) {
    merged = std::move(*result);
  }
  AppendAssets(reload_store_.Get(origin), &merged);
  AppendAssets(once_store_.Get(origin), &merged);
  if (merged.name.empty()) {
    return std::nullopt;
  }
  return merged;
}

bool DeveloperProfileTabHelper::ResetProfilesForUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  std::vector<DeveloperProfileChange> persistent_changes;
  std::vector<DeveloperProfileChange> reload_changes;
  std::vector<DeveloperProfileChange> once_changes;
  if (!BuildResetChanges(store_, url, tab_token_, /*persistent=*/true,
                         &persistent_changes) ||
      !BuildResetChanges(reload_store_, url, tab_token_, /*persistent=*/false,
                         &reload_changes) ||
      !BuildResetChanges(once_store_, url, tab_token_, /*persistent=*/false,
                         &once_changes)) {
    return false;
  }

  if (!ApplyProfileChanges(reload_store_, reload_changes)) {
    return false;
  }
  if (!ApplyProfileChanges(once_store_, once_changes)) {
    CHECK(RestoreProfileChanges(reload_store_, reload_changes));
    return false;
  }
  if (!ApplyProfileChanges(store_, persistent_changes)) {
    CHECK(RestoreProfileChanges(once_store_, once_changes));
    CHECK(RestoreProfileChanges(reload_store_, reload_changes));
    return false;
  }
  active_assets_.clear();
  return true;
}

std::vector<DeveloperAsset> DeveloperProfileTabHelper::TakeAssetsForNavigation(
    const GURL& url) {
  std::vector<DeveloperAsset> result =
      GetDeveloperAssetsForNavigation(store_, url, tab_token_);
  std::vector<DeveloperAsset> reload =
      GetDeveloperAssetsForNavigation(reload_store_, url, tab_token_);
  std::vector<DeveloperAsset> once =
      GetDeveloperAssetsForNavigation(once_store_, url, tab_token_);
  result.insert(result.end(), std::make_move_iterator(reload.begin()),
                std::make_move_iterator(reload.end()));
  result.insert(result.end(), std::make_move_iterator(once.begin()),
                std::make_move_iterator(once.end()));
  ClearTransientStore(once_store_);
  active_assets_ = result;
  return result;
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
  if (!web_contents || web_contents->IsBeingDestroyed()) {
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
