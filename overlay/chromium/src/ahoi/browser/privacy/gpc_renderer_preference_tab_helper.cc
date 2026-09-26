// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/gpc_renderer_preference_tab_helper.h"

#include "ahoi/browser/privacy/privacy_strict_request_rules.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "url/gurl.h"

namespace ahoi::privacy {

GpcRendererPreferenceTabHelper::GpcRendererPreferenceTabHelper(
    content::WebContents* web_contents,
    PrefService* prefs,
    bool is_off_the_record)
    : content::WebContentsObserver(web_contents),
      prefs_(prefs),
      is_off_the_record_(is_off_the_record) {
  if (web_contents) {
    UpdateFor(web_contents->GetLastCommittedURL());
  }
}

GpcRendererPreferenceTabHelper::~GpcRendererPreferenceTabHelper() = default;

void GpcRendererPreferenceTabHelper::SetWebContents(
    content::WebContents* web_contents) {
  Observe(web_contents);
  if (web_contents) {
    UpdateFor(web_contents->GetLastCommittedURL());
  }
}

void GpcRendererPreferenceTabHelper::DidStartNavigation(
    content::NavigationHandle* handle) {
  if (handle->IsInPrimaryMainFrame() && !handle->IsSameDocument()) {
    UpdateFor(handle->GetURL());
  }
}

void GpcRendererPreferenceTabHelper::DidRedirectNavigation(
    content::NavigationHandle* handle) {
  if (handle->IsInPrimaryMainFrame()) {
    UpdateFor(handle->GetURL());
  }
}

void GpcRendererPreferenceTabHelper::UpdateFor(const GURL& main_frame_url) {
  content::WebContents* contents = web_contents();
  if (!contents || !prefs_) {
    return;
  }
  const bool enable = GlobalPrivacyControlForMainFrameUrl(
      *prefs_, is_off_the_record_, main_frame_url);
  blink::RendererPreferences* renderer_prefs =
      contents->GetMutableRendererPrefs();
  if (renderer_prefs->enable_global_privacy_control == enable) {
    return;
  }
  renderer_prefs->enable_global_privacy_control = enable;
  contents->SyncRendererPrefs();
}

}  // namespace ahoi::privacy
