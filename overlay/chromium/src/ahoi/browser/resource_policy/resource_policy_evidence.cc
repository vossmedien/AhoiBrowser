// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/resource_policy/resource_policy_service_internal.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace ahoi::resource_policy {

ResourcePolicyEvidence ResourcePolicyService::CollectPerformanceEvidence()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ResourcePolicyEvidence evidence;
  evidence.memory_saver_enabled = IsMemorySaverEnabled();
  evidence.memory_saver_managed = IsMemorySaverManaged();
  evidence.tabs.reserve(tabs_.size());

  for (const auto& [tab, tracked] : tabs_) {
    if (!tracked->tab || !tab->GetContents()) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    const TabResourceStatus status = GetTabStatus(tab);
    TabPerformanceEvidence item;
    item.tab_handle = tab->GetHandle().raw_value();
    item.url = contents->GetVisibleURL().is_empty()
                   ? contents->GetLastCommittedURL()
                   : contents->GetVisibleURL();
    item.navigation_entry_count = static_cast<size_t>(
        std::max(0, contents->GetController().GetEntryCount()));
    item.state = status.state;
    item.block_reason = status.block_reason;
    item.auto_discardable = status.auto_discardable;

    if (status.state == TabLifecycleState::kSleeping) {
      ++evidence.sleeping_count;
      using PreDiscardResourceUsage = performance_manager::user_tuning::
          UserPerformanceTuningManager::PreDiscardResourceUsage;
      if (const auto* resource_usage =
              PreDiscardResourceUsage::FromWebContents(contents)) {
        item.estimated_freed_kib = static_cast<int64_t>(
            resource_usage->memory_footprint_estimate().InKiB());
        evidence.estimated_freed_kib += item.estimated_freed_kib;
      }
    } else if (status.state == TabLifecycleState::kWaking) {
      ++evidence.waking_count;
    } else if (status.state == TabLifecycleState::kAwake) {
      ++evidence.awake_count;
    }
    if (status.can_sleep) {
      ++evidence.eligible_count;
    }
    if (status.block_reason != SleepBlockReason::kNone &&
        status.block_reason != SleepBlockReason::kUnavailable) {
      ++evidence.protected_count;
    }
    evidence.tabs.push_back(std::move(item));
  }
  return evidence;
}

}  // namespace ahoi::resource_policy
