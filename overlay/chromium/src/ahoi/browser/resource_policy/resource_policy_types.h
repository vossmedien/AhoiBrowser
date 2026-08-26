// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_TYPES_H_
#define AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace ahoi::resource_policy {

// UI state is derived from Chromium's WebContents/lifecycle state. Ahoi never
// persists this enum and never treats a sleeping tab as closed or deleted.
enum class TabLifecycleState {
  kAwake = 0,
  kSleeping,
  kWaking,
  kUnavailable,
};

// The first matching reason is the user-facing primary reason. Values are not
// persisted; Chromium's CannotDiscardReason remains the metrics authority.
enum class SleepBlockReason {
  kNone = 0,
  kActivePane,
  kVisiblePane,
  kAudible,
  kRecentlyAudible,
  kMediaSession,
  kPictureInPicture,
  kCapture,
  kDownload,
  kUpload,
  kUnsavedForm,
  kBeforeUnload,
  kDevTools,
  kHttpAuth,
  kPermissionPrompt,
  kFileChooser,
  kModalFlow,
  kProductProtection,
  kNeverSleep,
  kEnterprisePolicy,
  kRecentlyVisible,
  kUpstreamPolicy,
  kUnavailable,
};

// Ahoi-only lifetime protections. Upstream signals such as active/visible,
// audible, PiP, capture, form interaction, DevTools and enterprise exceptions
// are still checked by DiscardEligibilityPolicy immediately before discard.
enum class CriticalFlow {
  kMiniPlayer = 0,
  kHttpAuth,
  kPermissionPrompt,
  kFileChooser,
  kBeforeUnload,
  kDownload,
  kUpload,
  kOtherModal,
  kMaxValue = kOtherModal,
};

struct CriticalSignals {
  bool active_pane = false;
  bool visible_pane = false;
  bool audible = false;
  bool recently_audible = false;
  bool media_session = false;
  bool picture_in_picture = false;
  bool capture = false;
  bool download = false;
  bool upload = false;
  bool unsaved_form = false;
  bool before_unload = false;
  bool devtools = false;
  bool http_auth = false;
  bool permission_prompt = false;
  bool file_chooser = false;
  bool modal_flow = false;
  bool product_protection = false;
  bool never_sleep = false;
  bool enterprise_policy = false;
  bool upstream_protected = false;
};

struct TabResourceStatus {
  TabLifecycleState state = TabLifecycleState::kUnavailable;
  SleepBlockReason block_reason = SleepBlockReason::kUnavailable;
  bool can_sleep = false;
  bool auto_discardable = false;
  bool never_sleep = false;
  bool memory_saver_enabled = false;
  bool memory_saver_managed = false;

  bool operator==(const TabResourceStatus&) const = default;
};

struct TabPerformanceEvidence {
  int tab_handle = 0;
  GURL url;
  size_t navigation_entry_count = 0;
  TabLifecycleState state = TabLifecycleState::kUnavailable;
  SleepBlockReason block_reason = SleepBlockReason::kUnavailable;
  bool auto_discardable = false;
  int64_t estimated_freed_kib = 0;
};

struct ResourcePolicyEvidence {
  bool memory_saver_enabled = false;
  bool memory_saver_managed = false;
  size_t awake_count = 0;
  size_t sleeping_count = 0;
  size_t waking_count = 0;
  size_t eligible_count = 0;
  size_t protected_count = 0;
  int64_t estimated_freed_kib = 0;
  std::vector<TabPerformanceEvidence> tabs;
};

SleepBlockReason GetPrimaryBlockReason(const CriticalSignals& signals);
bool HasAutomaticAhoiProtection(const CriticalSignals& signals);
std::string_view ToString(TabLifecycleState state);
std::string_view ToString(SleepBlockReason reason);

}  // namespace ahoi::resource_policy

#endif  // AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_TYPES_H_
