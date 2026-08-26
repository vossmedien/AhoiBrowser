// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/memory/tab_sleeping.h"

#include <algorithm>
#include <vector>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/resource_policy/resource_policy_service_factory.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/performance_manager/policies/page_discarding_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ahoi::memory {

namespace {

using CannotDiscardReason =
    performance_manager::policies::CannotDiscardReason;
using DiscardReason =
    performance_manager::policies::DiscardEligibilityPolicy::DiscardReason;

resource_policy::ResourcePolicyService* GetResourcePolicyService(
    tabs::TabInterface* tab) {
  return tab ? resource_policy::ResourcePolicyServiceFactory::GetForProfile(
                   tab->GetProfile())
             : nullptr;
}

SleepBlockReason MapResourcePolicyReason(
    resource_policy::SleepBlockReason reason) {
  using ResourceReason = resource_policy::SleepBlockReason;
  switch (reason) {
    case ResourceReason::kNone:
      return SleepBlockReason::kNone;
    case ResourceReason::kActivePane:
    case ResourceReason::kVisiblePane:
      return SleepBlockReason::kActiveTab;
    case ResourceReason::kAudible:
    case ResourceReason::kMediaSession:
      return SleepBlockReason::kAudible;
    case ResourceReason::kRecentlyAudible:
      return SleepBlockReason::kRecentlyAudible;
    case ResourceReason::kPictureInPicture:
      return SleepBlockReason::kPictureInPicture;
    case ResourceReason::kCapture:
      return SleepBlockReason::kCapture;
    case ResourceReason::kDownload:
    case ResourceReason::kUpload:
      return SleepBlockReason::kDownload;
    case ResourceReason::kUnsavedForm:
    case ResourceReason::kBeforeUnload:
      return SleepBlockReason::kFormState;
    case ResourceReason::kDevTools:
      return SleepBlockReason::kDevTools;
    case ResourceReason::kNeverSleep:
      return SleepBlockReason::kNeverSleep;
    case ResourceReason::kHttpAuth:
    case ResourceReason::kPermissionPrompt:
    case ResourceReason::kFileChooser:
    case ResourceReason::kModalFlow:
    case ResourceReason::kProductProtection:
    case ResourceReason::kEnterprisePolicy:
    case ResourceReason::kRecentlyVisible:
    case ResourceReason::kUpstreamPolicy:
      return SleepBlockReason::kUpstreamPolicy;
    case ResourceReason::kUnavailable:
      return SleepBlockReason::kUnavailable;
  }
  return SleepBlockReason::kUnavailable;
}

bool HasInProgressDownload(content::WebContents* contents) {
  if (!contents || !contents->GetBrowserContext()) {
    return false;
  }
  content::DownloadManager* manager =
      contents->GetBrowserContext()->GetDownloadManager();
  if (!manager) {
    return false;
  }

  content::DownloadManager::DownloadVector downloads;
  manager->GetAllDownloads(&downloads);
  return std::ranges::any_of(downloads, [contents](download::DownloadItem* item) {
    if (!item || item->IsDone()) {
      return false;
    }
    return content::DownloadItemUtils::GetOriginalWebContents(item) == contents ||
           content::DownloadItemUtils::GetWebContents(item) == contents;
  });
}

SleepBlockReason MapBlockedReason(CannotDiscardReason reason) {
  switch (reason) {
    case CannotDiscardReason::kActiveTab:
    case CannotDiscardReason::kVisible:
      return SleepBlockReason::kActiveTab;
    case CannotDiscardReason::kAudible:
      return SleepBlockReason::kAudible;
    case CannotDiscardReason::kRecentlyAudible:
      return SleepBlockReason::kRecentlyAudible;
    case CannotDiscardReason::kPictureInPicture:
      return SleepBlockReason::kPictureInPicture;
    case CannotDiscardReason::kCapturingVideo:
    case CannotDiscardReason::kCapturingAudio:
    case CannotDiscardReason::kBeingMirrored:
    case CannotDiscardReason::kCapturingWindow:
    case CannotDiscardReason::kCapturingDisplay:
      return SleepBlockReason::kCapture;
    case CannotDiscardReason::kDevToolsOpen:
      return SleepBlockReason::kDevTools;
    case CannotDiscardReason::kFormInteractions:
    case CannotDiscardReason::kUserEdits:
      return SleepBlockReason::kFormState;
    case CannotDiscardReason::kOptedOut:
      return SleepBlockReason::kNeverSleep;
    default:
      return SleepBlockReason::kUpstreamPolicy;
  }
}

bool GetUpstreamEligibility(tabs::TabInterface* tab,
                            SleepBlockReason* blocked_reason) {
  if (!tab || !performance_manager::PerformanceManager::IsAvailable()) {
    *blocked_reason = SleepBlockReason::kUnavailable;
    return false;
  }

  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    *blocked_reason = SleepBlockReason::kUnavailable;
    return false;
  }
  if (HasInProgressDownload(contents)) {
    *blocked_reason = SleepBlockReason::kDownload;
    return false;
  }

  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          contents);
  if (!page_node) {
    *blocked_reason = SleepBlockReason::kUnavailable;
    return false;
  }

  auto* policy =
      performance_manager::policies::DiscardEligibilityPolicy::GetFromGraph(
          performance_manager::PerformanceManager::GetGraph());
  if (!policy) {
    *blocked_reason = SleepBlockReason::kUnavailable;
    return false;
  }

  std::vector<CannotDiscardReason> reasons;
  const auto result = policy->CanDiscard(
      page_node.get(), DiscardReason::URGENT,
      /*ignore_recent_visibility=*/true, &reasons);
  if (result != performance_manager::policies::CanDiscardResult::kEligible) {
    *blocked_reason = reasons.empty() ? SleepBlockReason::kUpstreamPolicy
                                      : MapBlockedReason(reasons.front());
    return false;
  }

  *blocked_reason = SleepBlockReason::kNone;
  return true;
}

}  // namespace

bool IsManualSleepAllowed(const SleepEligibilityInputs& inputs) {
  return !inputs.active && !inputs.audible && !inputs.recently_audible &&
         !inputs.picture_in_picture && !inputs.capturing && !inputs.download &&
         !inputs.devtools && !inputs.form_state && !inputs.never_sleep &&
         !inputs.upstream_protected;
}

SleepBlockReason GetBlockedReason(const SleepEligibilityInputs& inputs) {
  if (inputs.active) {
    return SleepBlockReason::kActiveTab;
  }
  if (inputs.audible) {
    return SleepBlockReason::kAudible;
  }
  if (inputs.recently_audible) {
    return SleepBlockReason::kRecentlyAudible;
  }
  if (inputs.picture_in_picture) {
    return SleepBlockReason::kPictureInPicture;
  }
  if (inputs.capturing) {
    return SleepBlockReason::kCapture;
  }
  if (inputs.download) {
    return SleepBlockReason::kDownload;
  }
  if (inputs.devtools) {
    return SleepBlockReason::kDevTools;
  }
  if (inputs.form_state) {
    return SleepBlockReason::kFormState;
  }
  if (inputs.never_sleep) {
    return SleepBlockReason::kNeverSleep;
  }
  if (inputs.upstream_protected) {
    return SleepBlockReason::kUpstreamPolicy;
  }
  return SleepBlockReason::kNone;
}

TabSleepStatus GetTabSleepStatus(tabs::TabInterface* tab) {
  TabSleepStatus status;
  if (!tab) {
    return status;
  }

  if (auto* service = GetResourcePolicyService(tab)) {
    const resource_policy::TabResourceStatus resource_status =
        service->GetTabStatus(tab);
    switch (resource_status.state) {
      case resource_policy::TabLifecycleState::kAwake:
      case resource_policy::TabLifecycleState::kWaking:
        status.state = TabSleepState::kAwake;
        break;
      case resource_policy::TabLifecycleState::kSleeping:
        status.state = TabSleepState::kSleeping;
        break;
      case resource_policy::TabLifecycleState::kUnavailable:
        status.state = TabSleepState::kUnavailable;
        break;
    }
    status.block_reason =
        MapResourcePolicyReason(resource_status.block_reason);
    status.can_sleep = service->CanSleepTab(tab);
    status.never_sleep = resource_status.never_sleep;
    return status;
  }

  status.never_sleep = IsNeverSleep(tab);
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    status.block_reason = SleepBlockReason::kUnavailable;
    return status;
  }
  if (contents->WasDiscarded()) {
    status.state = TabSleepState::kSleeping;
    status.block_reason = SleepBlockReason::kNone;
    return status;
  }

  status.state = TabSleepState::kAwake;
  status.can_sleep = GetUpstreamEligibility(tab, &status.block_reason);
  if (status.never_sleep && status.can_sleep) {
    // The policy helper may not have received the pref observer update yet.
    status.can_sleep = false;
    status.block_reason = SleepBlockReason::kNeverSleep;
  }
  return status;
}

bool IsTabSleeping(tabs::TabInterface* tab) {
  // This is intentionally a cheap read. Sidebar projection can ask for the
  // visual status of every tab on every refresh; policy evaluation belongs to
  // the explicit Sleep/CanSleep action path below.
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return contents && contents->WasDiscarded();
}

bool CanSleepTab(tabs::TabInterface* tab) {
  if (auto* service = GetResourcePolicyService(tab)) {
    return service->CanSleepTab(tab);
  }
  return GetTabSleepStatus(tab).can_sleep;
}

bool SleepTab(tabs::TabInterface* tab) {
  if (auto* service = GetResourcePolicyService(tab)) {
    return service->SleepTab(tab);
  }
  if (!tab || !CanSleepTab(tab)) {
    return false;
  }

  content::WebContents* contents = tab->GetContents();
  if (!contents || tab->IsActivated() || HasInProgressDownload(contents)) {
    return false;
  }
  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          contents);
  if (!page_node || !performance_manager::PerformanceManager::IsAvailable()) {
    return false;
  }

  // PageDiscardingHelper performs the same eligibility check immediately
  // before dispatching to TabLifecycleUnitExternal. This is the upstream
  // replacement path that keeps SessionBridge's discard observer intact.
  auto* helper =
      performance_manager::policies::PageDiscardingHelper::GetFromGraph(
          performance_manager::PerformanceManager::GetGraph());
  if (!helper) {
    return false;
  }
  return helper->ImmediatelyDiscardMultiplePages(
      {page_node.get()}, DiscardReason::URGENT,
      /*ignore_recent_visibility=*/true);
}

bool WakeTab(tabs::TabInterface* tab) {
  if (auto* service = GetResourcePolicyService(tab)) {
    return service->WakeTab(tab);
  }
  if (!tab || !IsTabSleeping(tab)) {
    return false;
  }
  tab->LoadIfNeeded();
  return true;
}

std::string GetNeverSleepKey(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return std::string();
  }
  // Chromium's existing filter format understands scheme, host, port and
  // path. The explicit path wildcard scopes this exception to this origin,
  // while avoiding a new persistence format or policy store.
  return url::Origin::Create(url).Serialize() + "/*";
}

bool IsNeverSleepForUrl(PrefService* pref_service, const GURL& url) {
  if (!pref_service) {
    return false;
  }
  const std::string key = GetNeverSleepKey(url);
  return !key.empty() &&
         performance_manager::user_tuning::prefs::
             IsSiteInTabDiscardExceptionsList(pref_service, key);
}

bool SetNeverSleepForUrl(PrefService* pref_service,
                         const GURL& url,
                         bool enabled) {
  if (!pref_service) {
    return false;
  }
  const std::string key = GetNeverSleepKey(url);
  if (key.empty()) {
    return false;
  }

  if (enabled) {
    performance_manager::user_tuning::prefs::AddSiteToTabDiscardExceptionsList(
        pref_service, key);
    return true;
  }

  const base::DictValue& current = pref_service->GetDict(
      performance_manager::user_tuning::prefs::kTabDiscardingExceptionsWithTime);
  if (!current.contains(key)) {
    return true;
  }
  base::DictValue next = current.Clone();
  next.Remove(key);
  pref_service->SetDict(
      performance_manager::user_tuning::prefs::kTabDiscardingExceptionsWithTime,
      std::move(next));
  return true;
}

bool IsNeverSleep(tabs::TabInterface* tab) {
  if (auto* service = GetResourcePolicyService(tab)) {
    return service->IsNeverSleep(tab);
  }
  return tab && IsNeverSleepForUrl(tab->GetProfile()->GetPrefs(), tab->GetURL());
}

bool SetNeverSleep(tabs::TabInterface* tab, bool enabled) {
  if (auto* service = GetResourcePolicyService(tab)) {
    return service->SetNeverSleep(tab, enabled);
  }
  return tab && SetNeverSleepForUrl(tab->GetProfile()->GetPrefs(), tab->GetURL(),
                                    enabled);
}

}  // namespace ahoi::memory
