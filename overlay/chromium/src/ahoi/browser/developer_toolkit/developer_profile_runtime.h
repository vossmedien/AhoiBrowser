// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_

#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_profile_store.h"
#include "ahoi/browser/developer_toolkit/developer_secret_store.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
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

  void SetWebContents(content::WebContents* web_contents);

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  PrefDeveloperProfileStore store_;
};

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
  base::WeakPtrFactory<DeveloperProfileNavigationThrottle> weak_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_PROFILE_RUNTIME_H_
