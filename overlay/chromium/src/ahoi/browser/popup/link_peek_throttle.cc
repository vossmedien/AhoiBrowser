// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek_throttle.h"

#include <memory>

#include "ahoi/browser/popup/link_peek.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ahoi::popup {

void LinkPeekNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  content::WebContents* const contents = handle.GetWebContents();
  if (!contents || !handle.IsInPrimaryMainFrame() ||
      !handle.IsRendererInitiated() || !handle.HasUserGesture()) {
    return;
  }
  Profile* const profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  const PrefService* const prefs = profile ? profile->GetPrefs() : nullptr;
  if (!prefs || !prefs->FindPreference(kAutoPeekFromSavedPagesPref) ||
      !prefs->GetBoolean(kAutoPeekFromSavedPagesPref)) {
    return;
  }
  registry.AddThrottle(std::make_unique<LinkPeekNavigationThrottle>(registry));
}

LinkPeekNavigationThrottle::LinkPeekNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

LinkPeekNavigationThrottle::~LinkPeekNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
LinkPeekNavigationThrottle::WillStartRequest() {
  content::NavigationHandle* const handle = navigation_handle();
  content::WebContents* const contents = handle->GetWebContents();
  const GURL& url = handle->GetURL();
  const GURL& current = contents->GetLastCommittedURL();
  if (handle->IsPost() || !IsPeekableUrl(url) ||
      !ui::PageTransitionCoreTypeIs(handle->GetPageTransition(),
                                    ui::PAGE_TRANSITION_LINK) ||
      !current.SchemeIsHTTPOrHTTPS() ||
      net::registry_controlled_domains::SameDomainOrHost(
          current, url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES) ||
      !CanAutoPeekLink(contents, url)) {
    return PROCEED;
  }
  // The preview opens after this navigation has been cancelled, never while
  // the navigation is still running in the saved page.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<content::WebContents> opener, GURL url) {
                       if (opener) {
                         PeekLink(opener.get(), url);
                       }
                     },
                     contents->GetWeakPtr(), url));
  return CANCEL_AND_IGNORE;
}

const char* LinkPeekNavigationThrottle::GetNameForLogging() {
  return "AhoiLinkPeekNavigationThrottle";
}

}  // namespace ahoi::popup
