// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_INTERNAL_H_
#define AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_INTERNAL_H_

#include <array>
#include <memory>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"

namespace ahoi::resource_policy {

struct ResourcePolicyService::TrackedTab {
  static constexpr size_t kCriticalFlowCount =
      static_cast<size_t>(CriticalFlow::kMaxValue) + 1;

  explicit TrackedTab(tabs::TabInterface* tab);
  ~TrackedTab();

  bool HasExplicitProtection() const;
  bool HasExplicitProtection(CriticalFlow flow) const;

  base::WeakPtr<tabs::TabInterface> tab;
  raw_ptr<TabStripModel> model = nullptr;
  std::unique_ptr<TabSignalsObserver> signals;
  std::array<size_t, kCriticalFlowCount> protection_counts{};
  base::CallbackListSubscription will_discard_contents_subscription;
  base::CallbackListSubscription did_activate_subscription;
  base::CallbackListSubscription will_deactivate_subscription;
  base::CallbackListSubscription modal_ui_subscription;
  base::CallbackListSubscription blocked_state_subscription;
  bool waking = false;
  bool ahoi_auto_discard_block_applied = false;
  bool auto_discardable_before_ahoi_block = true;
  TabResourceStatus last_published_status;
  bool has_published_status = false;
};

}  // namespace ahoi::resource_policy

#endif  // AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_INTERNAL_H_
