// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_asset_validation.h"
#include "ahoi/browser/developer_toolkit/developer_profile_integration.h"
#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_profile_validation.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/check.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/document_loader_factory_refresh.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {
namespace {

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
  // A full reset also withdraws the CSP/CORS consent, so enabling advanced
  // response rules again shows the warning again (DEV-16).
  profile->response_header_advanced_mode_acknowledged = false;
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
    if (owner_origin == target_origin) {
      const DeveloperProfile before_disabling = after;
      DisablePersistentOverrides(&after);
      changed = changed || after != before_disabling;
    }
    if (!changed) {
      continue;
    }

    std::optional<DeveloperProfile> replacement;
    if (HasPersistentProfileData(after) || !after.headers_persistent) {
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

}  // namespace

bool DeveloperProfileTabHelper::SaveProfile(const url::Origin& origin,
                                            const DeveloperProfile& profile) {
  if (!HasBoundContext() || ValidateDeveloperProfile(origin, profile) !=
      DeveloperProfileValidationError::kNone) {
    return false;
  }

  DeveloperProfile persistent = profile;
  DeveloperProfile reload{.name = profile.name};
  DeveloperProfile once{.name = profile.name};
  if (!profile.headers_persistent) {
    reload.headers_persistent = false;
    reload.headers_owner_token = tab_token_;
    reload.header_rules_enabled = profile.header_rules_enabled;
    reload.header_rules = profile.header_rules;
    reload.response_header_rules_enabled = profile.response_header_rules_enabled;
    reload.response_header_rules = profile.response_header_rules;
    reload.response_header_advanced_mode_acknowledged =
        profile.response_header_advanced_mode_acknowledged;
    persistent.headers_persistent = true;
    persistent.headers_owner_token.clear();
    persistent.header_rules_enabled = false;
    persistent.header_rules_sync_enabled = false;
    persistent.header_rules.clear();
    persistent.response_header_rules_enabled = false;
    persistent.response_header_rules_sync_enabled = false;
    persistent.response_header_rules.clear();
    persistent.response_header_advanced_mode_acknowledged = false;
  }
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
      ((!reload.assets.empty() || !reload.headers_persistent) &&
       ValidateDeveloperProfile(origin, reload) !=
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
  if (!reload.assets.empty() || !reload.headers_persistent) {
    new_reload = std::move(reload);
  }
  if (!once.assets.empty()) {
    new_once = std::move(once);
  }

  // Prefs can be changed by sync or another surface while this tab retains
  // transient state. Prove that every new entry fits before mutating any of
  // the three stores. Temporary headers keep only user-supplied nonsecret
  // values/opaque Keychain references in this tab, never materialized secrets.
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
  if (!HasBoundContext()) {
    return false;
  }
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
  if (!HasBoundContext()) {
    return std::nullopt;
  }
  std::optional<DeveloperProfile> result = store_.Get(origin);
  DeveloperProfile merged;
  if (result) {
    merged = std::move(*result);
  }
  const auto reload = reload_store_.Get(origin);
  AppendAssets(reload, &merged);
  if (reload && !reload->headers_persistent) {
    merged.headers_persistent = false;
    merged.headers_owner_token = reload->headers_owner_token;
    merged.header_rules_enabled = reload->header_rules_enabled;
    merged.header_rules_sync_enabled = false;
    merged.header_rules = reload->header_rules;
    merged.response_header_rules_enabled = reload->response_header_rules_enabled;
    merged.response_header_rules_sync_enabled = false;
    merged.response_header_rules = reload->response_header_rules;
    merged.response_header_advanced_mode_acknowledged =
        reload->response_header_advanced_mode_acknowledged;
  }
  AppendAssets(once_store_.Get(origin), &merged);
  if (merged.name.empty()) {
    return std::nullopt;
  }
  return merged;
}

bool DeveloperProfileTabHelper::HasTemporaryHeaders() const {
  if (!HasBoundContext()) {
    return false;
  }
  for (const auto& origin : reload_store_.ListOrigins()) {
    const auto profile = reload_store_.Get(origin);
    if (profile && !profile->headers_persistent) {
      return true;
    }
  }
  return false;
}

bool DeveloperProfileTabHelper::IsCacheDisabledForCurrentTab() const {
  return HasBoundContext() &&
         developer_toolkit_prefs::IsToolkitEnabled(*prefs_) &&
         cache_disabled_for_tab_;
}

bool DeveloperProfileTabHelper::SetCacheDisabledForCurrentTab(bool disabled) {
  if (!HasBoundContext() ||
      !developer_toolkit_prefs::IsToolkitEnabled(*prefs_) ||
      !web_contents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (cache_disabled_for_tab_ == disabled) {
    return true;
  }
  cache_disabled_for_tab_ = disabled;
  // Retire in-flight approvals without rolling back the generation. Renew the
  // current document's native factories before requesting a reload: Chromium's
  // before-unload/repost dialog can keep this same document alive.
  ++activation_generation_;
  ClearDeveloperProfileNavigationRequest(*web_contents());
  const GURL committed_url = web_contents()->GetLastCommittedURL();
  UpdateDeveloperProfileNetworkState(
      *web_contents(), committed_url,
      GetDeveloperNetworkProfileForTab(prefs_, web_contents(), committed_url));
  if (auto* main_frame = web_contents()->GetPrimaryMainFrame()) {
    main_frame->ForEachRenderFrameHostWithAction(
        [this](content::RenderFrameHost* frame) {
          // Inner WebContents retain their own native context and policy.
          if (content::WebContents::FromRenderFrameHost(frame) != web_contents()) {
            return content::RenderFrameHost::FrameIterationAction::kSkipChildren;
          }
          content::RecreateDocumentSubresourceLoaderFactories(*frame);
          return content::RenderFrameHost::FrameIterationAction::kContinue;
        });
  }
  return true;
}

std::optional<DeveloperProfile> GetDeveloperProfileForTab(
    PrefService* prefs,
    content::WebContents* web_contents,
    const GURL& url) {
  if (!prefs || !web_contents || web_contents->IsBeingDestroyed() ||
      !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      !developer_toolkit_prefs::IsToolkitEnabled(*prefs)) {
    return std::nullopt;
  }
  auto* context = web_contents->GetBrowserContext();
  if (!context || context->IsOffTheRecord() ||
      !user_prefs::UserPrefs::IsInitialized(context) ||
      user_prefs::UserPrefs::Get(context) != prefs) {
    return std::nullopt;
  }
  if (auto* helper = DeveloperProfileTabHelper::FromWebContents(web_contents)) {
    return helper->GetProfile(url::Origin::Create(url));
  }
  PrefDeveloperProfileStore store(prefs, false);
  return GetDeveloperProfileForNavigation(store, url);
}

std::optional<DeveloperProfile> GetDeveloperNetworkProfileForTab(
    PrefService* prefs,
    content::WebContents* web_contents,
    const GURL& url) {
  auto profile = GetDeveloperProfileForTab(prefs, web_contents, url);
  // A null saved profile is normal, but a rejected foreign/OTR context must
  // not be converted into an approved tab-only override.
  if (!prefs || !web_contents || web_contents->IsBeingDestroyed() ||
      !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      !developer_toolkit_prefs::IsToolkitEnabled(*prefs)) {
    return std::nullopt;
  }
  auto* context = web_contents->GetBrowserContext();
  if (!context || context->IsOffTheRecord() ||
      !user_prefs::UserPrefs::IsInitialized(context) ||
      user_prefs::UserPrefs::Get(context) != prefs) {
    return std::nullopt;
  }
  const auto* helper = DeveloperProfileTabHelper::FromWebContents(web_contents);
  if (helper && helper->IsCacheDisabledForCurrentTab()) {
    if (!profile) {
      profile = DeveloperProfile{};
    }
    profile->cache_disabled = true;
  }
  return profile;
}

bool DeveloperProfileTabHelper::ResetProfilesForUrl(const GURL& url) {
  if (!HasBoundContext() || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
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
  cache_disabled_for_tab_ = false;
  ++activation_generation_;
  return true;
}

std::vector<DeveloperAsset> DeveloperProfileTabHelper::TakeAssetsForNavigation(
    const GURL& url) {
  if (!HasBoundContext() || !developer_toolkit_prefs::IsToolkitEnabled(*prefs_)) {
    active_assets_.clear();
    return {};
  }
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

bool DeveloperProfileTabHelper::HasBoundContext() const {
  if (!prefs_ || !web_contents() || web_contents()->IsBeingDestroyed()) {
    return false;
  }
  auto* context = web_contents()->GetBrowserContext();
  return context && !context->IsOffTheRecord() &&
         user_prefs::UserPrefs::IsInitialized(context) &&
         user_prefs::UserPrefs::Get(context) == prefs_;
}

}  // namespace ahoi
