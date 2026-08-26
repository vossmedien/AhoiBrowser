// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/resource_policy/resource_policy_service.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/resource_policy/resource_policy_service_internal.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/performance_manager/policies/page_discarding_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"

namespace ahoi::resource_policy {

namespace {

using CannotDiscardReason = performance_manager::policies::CannotDiscardReason;
using DiscardEligibilityPolicy =
    performance_manager::policies::DiscardEligibilityPolicy;

std::string NeverSleepKey(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return std::string();
  }
  return url::Origin::Create(url).Serialize() + "/*";
}

SleepBlockReason MapUpstreamReason(CannotDiscardReason reason,
                                   bool is_never_sleep) {
  switch (reason) {
    case CannotDiscardReason::kActiveTab:
      return SleepBlockReason::kActivePane;
    case CannotDiscardReason::kVisible:
      return SleepBlockReason::kVisiblePane;
    case CannotDiscardReason::kAudible:
      return SleepBlockReason::kAudible;
    case CannotDiscardReason::kRecentlyAudible:
      return SleepBlockReason::kRecentlyAudible;
    case CannotDiscardReason::kRecentlyVisible:
      return SleepBlockReason::kRecentlyVisible;
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
      return SleepBlockReason::kUnsavedForm;
    case CannotDiscardReason::kOptedOut:
      return is_never_sleep ? SleepBlockReason::kNeverSleep
                            : SleepBlockReason::kEnterprisePolicy;
    default:
      return SleepBlockReason::kUpstreamPolicy;
  }
}

}  // namespace

ResourcePolicyService::ResourcePolicyService(Profile* profile)
    : profile_(profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile_ || !profile_->IsRegularProfile() ||
      profile_->IsOffTheRecord() || !profile_->AllowsBrowserWindows()) {
    shutting_down_ = true;
    return;
  }
  // This keyed service is created while ProfileImpl is still constructing.
  // In particular, the profile's proto database provider is installed only
  // during ProfileImpl::DoFinalInit(), so GetDownloadManager() is not safe yet.
  // Observe the explicit profile lifecycle boundary instead of relying on a
  // posted task whose ordering would be an undocumented implementation detail.
  profile_observation_.Observe(profile_);
}

ResourcePolicyService::~ResourcePolicyService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

void ResourcePolicyService::OnProfileInitializationComplete(Profile* profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(profile_, profile);
  if (shutting_down_ || profile != profile_) {
    return;
  }
  profile_observation_.Reset();
  InitializeRuntime();
}

TabResourceStatus ResourcePolicyService::GetTabStatus(
    tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TabResourceStatus status;
  status.memory_saver_enabled = IsMemorySaverEnabled();
  status.memory_saver_managed = IsMemorySaverManaged();
  if (!tab || !profile_ || tab->GetProfile() != profile_) {
    return status;
  }

  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return status;
  }

  const auto tracked = tabs_.find(tab);
  if (tracked != tabs_.end() && tracked->second->waking) {
    status.state = TabLifecycleState::kWaking;
  } else if (contents->WasDiscarded()) {
    status.state = TabLifecycleState::kSleeping;
  } else {
    status.state = TabLifecycleState::kAwake;
  }

  status.never_sleep = IsNeverSleep(tab);
  if (auto* lifecycle =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents)) {
    status.auto_discardable = lifecycle->IsAutoDiscardable();
  }
  if (status.state == TabLifecycleState::kSleeping ||
      status.state == TabLifecycleState::kWaking) {
    status.block_reason = SleepBlockReason::kNone;
    return status;
  }

  const CriticalSignals signals = CollectCriticalSignals(tab);
  status.block_reason = GetPrimaryBlockReason(signals);
  if (status.block_reason != SleepBlockReason::kNone) {
    return status;
  }
  bool eligible = false;
  status.block_reason = GetUpstreamBlockReason(
      tab, /*ignore_recent_visibility=*/false, &eligible);
  status.can_sleep = eligible;
  return status;
}

bool ResourcePolicyService::IsTabSleeping(tabs::TabInterface* tab) const {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return contents && contents->WasDiscarded();
}

bool ResourcePolicyService::CanSleepTab(tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab || IsTabSleeping(tab) ||
      GetPrimaryBlockReason(CollectCriticalSignals(tab)) !=
          SleepBlockReason::kNone) {
    return false;
  }
  bool eligible = false;
  std::ignore =
      GetUpstreamBlockReason(tab, /*ignore_recent_visibility=*/true, &eligible);
  return eligible;
}

bool ResourcePolicyService::SleepTab(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!CanSleepTab(tab) ||
      !performance_manager::PerformanceManager::IsAvailable()) {
    return false;
  }
  content::WebContents* contents = tab->GetContents();
  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          contents);
  if (!page_node) {
    return false;
  }
  auto* helper =
      performance_manager::policies::PageDiscardingHelper::GetFromGraph(
          performance_manager::PerformanceManager::GetGraph());
  if (!helper ||
      !helper->ImmediatelyDiscardMultiplePages(
          {page_node.get()}, DiscardEligibilityPolicy::DiscardReason::PROACTIVE,
          /*ignore_recent_visibility=*/true)) {
    return false;
  }
  RefreshTab(tab);
  return true;
}

bool ResourcePolicyService::WakeTab(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab || !IsTabSleeping(tab)) {
    return false;
  }
  SetWaking(tab, true);
  tab->LoadIfNeeded();
  return true;
}

bool ResourcePolicyService::IsNeverSleep(tabs::TabInterface* tab) const {
  if (!tab || !tab->GetProfile() || !tab->GetProfile()->GetPrefs()) {
    return false;
  }
  const std::string key = NeverSleepKey(tab->GetURL());
  return !key.empty() && performance_manager::user_tuning::prefs::
                             IsSiteInTabDiscardExceptionsList(
                                 tab->GetProfile()->GetPrefs(), key);
}

bool ResourcePolicyService::SetNeverSleep(tabs::TabInterface* tab,
                                          bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab || tab->GetProfile() != profile_) {
    return false;
  }
  PrefService* prefs = profile_->GetPrefs();
  const std::string key = NeverSleepKey(tab->GetURL());
  if (!prefs || key.empty()) {
    return false;
  }
  if (enabled) {
    performance_manager::user_tuning::prefs::AddSiteToTabDiscardExceptionsList(
        prefs, key);
  } else {
    const base::DictValue& current =
        prefs->GetDict(performance_manager::user_tuning::prefs::
                           kTabDiscardingExceptionsWithTime);
    if (current.contains(key)) {
      base::DictValue next = current.Clone();
      next.Remove(key);
      prefs->SetDict(performance_manager::user_tuning::prefs::
                         kTabDiscardingExceptionsWithTime,
                     std::move(next));
    }
  }
  RefreshAllTabs();
  return true;
}

bool ResourcePolicyService::IsMemorySaverEnabled() const {
  return performance_manager::user_tuning::UserPerformanceTuningManager::
             HasInstance() &&
         performance_manager::user_tuning::UserPerformanceTuningManager::
             GetInstance()
                 ->IsMemorySaverModeActive();
}

bool ResourcePolicyService::IsMemorySaverManaged() const {
  return performance_manager::user_tuning::UserPerformanceTuningManager::
             HasInstance() &&
         performance_manager::user_tuning::UserPerformanceTuningManager::
             GetInstance()
                 ->IsMemorySaverModeManaged();
}

bool ResourcePolicyService::SetMemorySaverEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!performance_manager::user_tuning::UserPerformanceTuningManager::
          HasInstance() ||
      IsMemorySaverManaged()) {
    return false;
  }
  performance_manager::user_tuning::UserPerformanceTuningManager::GetInstance()
      ->SetMemorySaverModeEnabled(enabled);
  return true;
}

base::ScopedClosureRunner ResourcePolicyService::AcquireCriticalFlowProtection(
    tabs::TabInterface* tab,
    CriticalFlow flow) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab || tab->GetProfile() != profile_) {
    return base::ScopedClosureRunner();
  }
  if (!tabs_.contains(tab)) {
    BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
    TrackBrowser(browser);
    if (browser) {
      TrackTab(browser->GetTabStripModel(), tab);
    }
  }
  if (!tabs_.contains(tab)) {
    return base::ScopedClosureRunner();
  }
  AddCriticalFlowProtection(tab, flow);
  return base::ScopedClosureRunner(
      base::BindOnce(&ResourcePolicyService::ReleaseCriticalFlowProtection,
                     weak_ptr_factory_.GetWeakPtr(), tab->GetWeakPtr(), flow));
}

base::CallbackListSubscription ResourcePolicyService::AddStatusChangedCallback(
    StatusChangedCallback callback) {
  return status_changed_callbacks_.Add(std::move(callback));
}

SleepBlockReason ResourcePolicyService::GetUpstreamBlockReason(
    tabs::TabInterface* tab,
    bool ignore_recent_visibility,
    bool* eligible) const {
  *eligible = false;
  if (!tab || !performance_manager::PerformanceManager::IsAvailable()) {
    return SleepBlockReason::kUnavailable;
  }
  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          tab->GetContents());
  auto* policy =
      performance_manager::policies::DiscardEligibilityPolicy::GetFromGraph(
          performance_manager::PerformanceManager::GetGraph());
  if (!page_node || !policy) {
    return SleepBlockReason::kUnavailable;
  }
  std::vector<CannotDiscardReason> reasons;
  const auto result = policy->CanDiscard(
      page_node.get(), DiscardEligibilityPolicy::DiscardReason::PROACTIVE,
      ignore_recent_visibility, &reasons);
  *eligible =
      result == performance_manager::policies::CanDiscardResult::kEligible;
  if (*eligible) {
    return SleepBlockReason::kNone;
  }
  return reasons.empty()
             ? SleepBlockReason::kUpstreamPolicy
             : MapUpstreamReason(reasons.front(), IsNeverSleep(tab));
}

void ResourcePolicyService::ApplyAutomaticProtection(
    tabs::TabInterface* tab,
    const CriticalSignals& signals) {
  auto it = tabs_.find(tab);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  auto* lifecycle =
      contents
          ? resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
                contents)
          : nullptr;
  if (it == tabs_.end() || !lifecycle) {
    return;
  }
  TrackedTab& tracked = *it->second;
  const bool should_block = HasAutomaticAhoiProtection(signals);
  if (should_block && !tracked.ahoi_auto_discard_block_applied) {
    tracked.auto_discardable_before_ahoi_block = lifecycle->IsAutoDiscardable();
    lifecycle->SetAutoDiscardable(false);
    tracked.ahoi_auto_discard_block_applied = true;
  } else if (!should_block && tracked.ahoi_auto_discard_block_applied) {
    if (tracked.auto_discardable_before_ahoi_block) {
      lifecycle->SetAutoDiscardable(true);
    }
    tracked.ahoi_auto_discard_block_applied = false;
  }
}

void ResourcePolicyService::AddCriticalFlowProtection(tabs::TabInterface* tab,
                                                      CriticalFlow flow) {
  auto it = tabs_.find(tab);
  const size_t index = static_cast<size_t>(flow);
  if (it == tabs_.end() || index >= TrackedTab::kCriticalFlowCount) {
    return;
  }
  ++it->second->protection_counts[index];
  RefreshTab(tab);
}

void ResourcePolicyService::ReleaseCriticalFlowProtection(
    base::WeakPtr<tabs::TabInterface> tab,
    CriticalFlow flow) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab) {
    return;
  }
  auto it = tabs_.find(tab.get());
  const size_t index = static_cast<size_t>(flow);
  if (it == tabs_.end() || index >= TrackedTab::kCriticalFlowCount ||
      it->second->protection_counts[index] == 0) {
    return;
  }
  --it->second->protection_counts[index];
  RefreshTab(tab.get());
}

void ResourcePolicyService::OnMemorySaverModeChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RefreshAllTabs();
}

}  // namespace ahoi::resource_policy
