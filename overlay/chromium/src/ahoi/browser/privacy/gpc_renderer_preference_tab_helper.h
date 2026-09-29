// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_GPC_RENDERER_PREFERENCE_TAB_HELPER_H_
#define AHOI_BROWSER_PRIVACY_GPC_RENDERER_PREFERENCE_TAB_HELPER_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"

class GURL;
class PrefService;

namespace ahoi::privacy {

// Keeps blink::RendererPreferences::enable_global_privacy_control of one tab
// in line with the mode of its top-level site (handoff 070). It updates at
// the start (and on each redirect) of every primary main-frame navigation,
// i.e. before the new document's view is created in a new process and long
// before commit in the same process, so the committed document normally
// starts with the right value. Owned by TabFeatures like the developer
// profile helper; the browser-side factory proxy (068) stays the
// authoritative source of the header.
class GpcRendererPreferenceTabHelper final
    : public content::WebContentsObserver {
 public:
  GpcRendererPreferenceTabHelper(content::WebContents* web_contents,
                                 PrefService* prefs,
                                 bool is_off_the_record);
  GpcRendererPreferenceTabHelper(const GpcRendererPreferenceTabHelper&) =
      delete;
  GpcRendererPreferenceTabHelper& operator=(
      const GpcRendererPreferenceTabHelper&) = delete;
  ~GpcRendererPreferenceTabHelper() override;

  // After a discard replaced the tab's WebContents.
  void SetWebContents(content::WebContents* web_contents);

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override;
  void DidRedirectNavigation(content::NavigationHandle* handle) override;

 private:
  void UpdateFor(const GURL& main_frame_url);

  const raw_ptr<PrefService> prefs_;
  const bool is_off_the_record_;
};

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_GPC_RENDERER_PREFERENCE_TAB_HELPER_H_
