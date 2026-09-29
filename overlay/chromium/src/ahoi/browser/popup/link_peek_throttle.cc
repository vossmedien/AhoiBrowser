// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek_throttle.h"

#include <memory>
#include <utility>

#include "ahoi/browser/popup/link_peek.h"
#include "ahoi/browser/popup/link_peek_input.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ahoi::popup {

void LinkPeekNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  content::WebContents* const contents = handle.GetWebContents();
  if (!contents || !handle.IsInPrimaryMainFrame()) {
    return;
  }
  // Every page learns its input before its first link click, so a later
  // target=_blank click can be told from a modifier click (see
  // PopupOverlayController::TryAutoPeekNewWindow) even when the option is
  // switched on while the page is already open.
  LinkPeekInputTracker::Track(contents);
  if (!handle.IsRendererInitiated() || !handle.HasUserGesture()) {
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
  if (handle->IsPost() || !IsPeekableUrl(url) ||
      !ui::PageTransitionCoreTypeIs(handle->GetPageTransition(),
                                    ui::PAGE_TRANSITION_LINK) ||
      !LeavesSite(contents->GetLastCommittedURL(), url) ||
      !CanAutoPeekLink(contents, url)) {
    return PROCEED;
  }
  // The preview repeats the link's own request: its referrer (already
  // reduced by rel=noreferrer and the page's Referrer-Policy) and its
  // initiator, which is the clicked frame's origin for a link in an iframe.
  PeekRequest request(url);
  request.referrer = content::Referrer(handle->GetReferrer());
  request.initiator_origin = handle->GetInitiatorOrigin();
  // The preview opens after this navigation has been cancelled, never while
  // the navigation is still running in the saved page, and only while the
  // page still shows the document the link was clicked in.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> opener,
             content::WeakDocumentPtr document, PeekRequest request) {
            if (opener && document.AsRenderFrameHostIfValid() ==
                              opener->GetPrimaryMainFrame()) {
              PeekLink(opener.get(), request);
            }
          },
          contents->GetWeakPtr(),
          contents->GetPrimaryMainFrame()->GetWeakDocumentPtr(),
          std::move(request)));
  return CANCEL_AND_IGNORE;
}

const char* LinkPeekNavigationThrottle::GetNameForLogging() {
  return "AhoiLinkPeekNavigationThrottle";
}

}  // namespace ahoi::popup
