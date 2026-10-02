// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_

#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"
#include "url/origin.h"

class PrefService;

namespace content {
class NavigationThrottleRegistry;
}

namespace ahoi {

// Applies the saved document portion immediately to the currently committed
// primary document. CSS is replaced idempotently; enabled JavaScript runs once
// in Ahoi's isolated world.
bool ApplyDeveloperProfileToCurrentDocument(content::WebContents& web_contents,
                                            const DeveloperProfile& profile);

// Applies already scope-resolved assets. LESS/SASS use only bounded CSS saved
// by the current sandboxed compiler version; raw preprocessor source is never
// injected. Main World JavaScript uses its transient restricted adapter.
bool ApplyDeveloperAssetsToCurrentDocument(
    content::WebContents& web_contents,
    const std::vector<DeveloperAsset>& assets);

// Applies or removes only Ahoi's own UA override. Existing overrides owned by
// another Chromium surface are never cleared.
void ApplyAhoiUserAgentOverride(content::WebContents& web_contents,
                                const DeveloperProfile* profile);
bool HasAhoiUserAgentOverride(content::WebContents& web_contents);

// Per-tab observer for persisted CSS/JavaScript. It performs a single pref
// lookup after a primary document commits and owns no timers/background work.
class DeveloperProfileTabHelper final : public content::WebContentsObserver {
 public:
  DeveloperProfileTabHelper(content::WebContents* web_contents,
                            PrefService* prefs);
  DeveloperProfileTabHelper(const DeveloperProfileTabHelper&) = delete;
  DeveloperProfileTabHelper& operator=(const DeveloperProfileTabHelper&) =
      delete;
  ~DeveloperProfileTabHelper() override;

  static DeveloperProfileTabHelper* FromWebContents(
      content::WebContents* web_contents);

  void SetWebContents(content::WebContents* web_contents);
  const std::string& tab_token() const { return tab_token_; }
  uint64_t activation_generation() const { return activation_generation_; }

  // Stores restart assets in the profile PrefService and keeps once/reload
  // assets in this tab-owned helper. Transient source never reaches prefs or
  // sync, and a current-tab scope is accepted only for this helper's token.
  bool SaveProfile(const url::Origin& origin, const DeveloperProfile& profile);
  bool RemoveProfile(const url::Origin& origin);
  std::optional<DeveloperProfile> GetProfile(const url::Origin& origin) const;
  bool HasTemporaryHeaders() const;
  // Explicit cache choice belongs to this native tab, survives reload and is
  // never merged into the editor's saved origin profile or serialized.
  bool IsCacheDisabledForCurrentTab() const;
  bool SetCacheDisabledForCurrentTab(bool disabled);

  // Removes every saved or tab-local asset that currently contributes to
  // `url`, while preserving unrelated assets owned by the same profile.
  // Exact-origin request-stage overrides are disabled as well. The operation
  // also clears the committed snapshot so reset cannot leave stale chips.
  bool ResetProfilesForUrl(const GURL& url);

  // Resolves all persistent and tab-local assets for one committed document.
  // Once-assets are consumed at this navigation boundary whether or not their
  // scope matched, so a later navigation can never resurrect them.
  std::vector<DeveloperAsset> TakeAssetsForNavigation(const GURL& url);
  const std::vector<DeveloperAsset>& active_assets() const {
    return active_assets_;
  }

  // content::WebContentsObserver:
  // Chromium accepts a navigation's user-agent override decision only while
  // DidStartNavigation observers run (NavigationRequest::
  // SetIsOverridingUserAgent CHECKs otherwise), so it is made here and not in
  // DeveloperProfileNavigationThrottle.
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  void AttachToWebContents(content::WebContents* web_contents);
  void DetachFromWebContents(content::WebContents* web_contents);
  void InitializeActivationObserver();
  void OnToolkitActivationChanged();
  bool HasBoundContext() const;

  const raw_ptr<PrefService> prefs_;
  PrefDeveloperProfileStore store_;
  InMemoryDeveloperProfileStore reload_store_;
  InMemoryDeveloperProfileStore once_store_;
  const std::string tab_token_;
  std::vector<DeveloperAsset> active_assets_;
  bool toolkit_active_ = false;
  bool cache_disabled_for_tab_ = false;
  uint64_t activation_generation_ = 0;
  PrefChangeRegistrar activation_pref_registrar_;
  base::WeakPtrFactory<DeveloperProfileTabHelper> weak_factory_{this};
};

// Resolves only the actual native tab's context and local header lifetime.
// Callers use this at navigation boundaries and request revalidation; there
// is no global lookup that assigns an arbitrary tab to a worker.
std::optional<DeveloperProfile> GetDeveloperProfileForTab(
    PrefService* prefs,
    content::WebContents* web_contents,
    const GURL& url);

// Network-only view: saved/tab header metadata plus the tab's cache choice.
// Editors must keep using GetDeveloperProfileForTab to avoid persisting this
// local choice while saving unrelated site settings or snippets.
std::optional<DeveloperProfile> GetDeveloperNetworkProfileForTab(
    PrefService* prefs,
    content::WebContents* web_contents,
    const GURL& url);

// Request-stage half of the feature. It is created only when the regular
// profile actually contains developer profiles. Opaque header secrets defer
// the initial primary request while both rule directions resolve atomically on
// a MayBlock worker; redirects remain same-origin or clear the snapshot.
class DeveloperProfileNavigationThrottle final
    : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  DeveloperProfileNavigationThrottle(
      content::NavigationThrottleRegistry& registry,
      PrefService* prefs);
  DeveloperProfileNavigationThrottle(
      content::NavigationThrottleRegistry& registry,
      PrefService* prefs,
      DeveloperSecretStoreFactory secret_store_factory);
  DeveloperProfileNavigationThrottle(
      const DeveloperProfileNavigationThrottle&) = delete;
  DeveloperProfileNavigationThrottle& operator=(
      const DeveloperProfileNavigationThrottle&) = delete;
  ~DeveloperProfileNavigationThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult ApplyInitialRequestOverrides();
  void OnHeaderSecretsMaterialized(
      int64_t navigation_id,
      GURL request_url,
      url::Origin origin,
      DeveloperProfile source_profile,
      std::optional<DeveloperProfile> materialized_profile);

  const raw_ptr<PrefService> prefs_;
  PrefDeveloperProfileStore store_;
  DeveloperSecretStoreFactory secret_store_factory_;
  base::WeakPtr<content::WebContents> web_contents_;
  const int64_t navigation_id_;
  const uint64_t activation_generation_;
  base::WeakPtrFactory<DeveloperProfileNavigationThrottle> weak_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_
