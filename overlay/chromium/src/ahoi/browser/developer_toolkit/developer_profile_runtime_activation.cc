// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_profile_runtime.h"

#include "ahoi/browser/developer_toolkit/developer_profile_url_loader_throttle.h"
#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/functional/bind.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

void DeveloperProfileTabHelper::InitializeActivationObserver() {
  if (!prefs_ || !prefs_->FindPreference(developer_toolkit_prefs::kToolkitEnabled)) {
    return;
  }
  toolkit_active_ = developer_toolkit_prefs::IsToolkitEnabled(*prefs_);
  activation_pref_registrar_.Init(prefs_);
  for (const char* key : {developer_toolkit_prefs::kToolkitEnabled,
                          developer_toolkit_prefs::kShowCookieButton,
                          developer_toolkit_prefs::kShowCacheButton,
                          developer_toolkit_prefs::kShowToolkitButton}) {
    activation_pref_registrar_.Add(key, base::BindRepeating(
        &DeveloperProfileTabHelper::OnToolkitActivationChanged,
        weak_factory_.GetWeakPtr()));
  }
}

void DeveloperProfileTabHelper::OnToolkitActivationChanged() {
  const bool enabled = developer_toolkit_prefs::IsToolkitEnabled(*prefs_);
  if (enabled == toolkit_active_) {
    return;
  }
  toolkit_active_ = enabled;
  ++activation_generation_;
  if (enabled || !web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }
  const bool modified_document = !active_assets_.empty() ||
                                HasAhoiUserAgentOverride(*web_contents());
  active_assets_.clear();
  ClearDeveloperProfileNavigationRequest(*web_contents());
  UpdateDeveloperProfileNetworkState(*web_contents(),
                                     web_contents()->GetLastCommittedURL(),
                                     std::nullopt);
  ApplyAhoiUserAgentOverride(*web_contents(), nullptr);
  if (modified_document &&
      web_contents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    // Arbitrary injected JS cannot be safely undone. Reload only the affected
    // native document so its previous script/DOM work cannot stay hidden.
    web_contents()->GetController().Reload(content::ReloadType::NORMAL, true);
  }
  // Stored profiles and transient drafts remain intact; enabling starts no
  // replay. A fresh explicit save/navigation belongs to the new generation.
}

}  // namespace ahoi
