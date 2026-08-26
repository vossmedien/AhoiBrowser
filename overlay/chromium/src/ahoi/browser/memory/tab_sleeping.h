// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEMORY_TAB_SLEEPING_H_
#define AHOI_BROWSER_MEMORY_TAB_SLEEPING_H_

#include <string>

#include "base/values.h"

class GURL;
class PrefService;

namespace tabs {
class TabInterface;
}

namespace ahoi::memory {

// Ahoi deliberately exposes Chromium's existing discarded-WebContents state;
// it does not maintain a second tab lifecycle or a second tab store.
enum class TabSleepState {
  kAwake = 0,
  kSleeping,
  kUnavailable,
};

enum class SleepBlockReason {
  kNone = 0,
  kActiveTab,
  kAudible,
  kRecentlyAudible,
  kPictureInPicture,
  kCapture,
  kDownload,
  kDevTools,
  kFormState,
  kNeverSleep,
  kUpstreamPolicy,
  kUnavailable,
};

struct TabSleepStatus {
  TabSleepState state = TabSleepState::kUnavailable;
  SleepBlockReason block_reason = SleepBlockReason::kUnavailable;
  bool can_sleep = false;
  bool never_sleep = false;
};

// This input-only policy seam keeps the critical-tab contract testable without
// fabricating a second lifecycle engine in production code.
struct SleepEligibilityInputs {
  bool active = false;
  bool audible = false;
  bool recently_audible = false;
  bool picture_in_picture = false;
  bool capturing = false;
  bool download = false;
  bool devtools = false;
  bool form_state = false;
  bool never_sleep = false;
  bool upstream_protected = false;
};

bool IsManualSleepAllowed(const SleepEligibilityInputs& inputs);
SleepBlockReason GetBlockedReason(const SleepEligibilityInputs& inputs);

TabSleepStatus GetTabSleepStatus(tabs::TabInterface* tab);
bool IsTabSleeping(tabs::TabInterface* tab);
bool CanSleepTab(tabs::TabInterface* tab);

// Uses PageDiscardingHelper/DiscardEligibilityPolicy and then Chromium's
// lifecycle discarder. In particular, this preserves TabInterface's
// WillDiscardContents replacement callback used by SessionBridge.
bool SleepTab(tabs::TabInterface* tab);
bool WakeTab(tabs::TabInterface* tab);

// The preference key is the origin pattern understood by Chromium's existing
// kTabDiscardingExceptionsWithTime policy (for example,
// "https://example.test/*").
std::string GetNeverSleepKey(const GURL& url);
bool IsNeverSleepForUrl(PrefService* pref_service, const GURL& url);
bool SetNeverSleepForUrl(PrefService* pref_service,
                         const GURL& url,
                         bool enabled);

bool IsNeverSleep(tabs::TabInterface* tab);
bool SetNeverSleep(tabs::TabInterface* tab, bool enabled);

}  // namespace ahoi::memory

#endif  // AHOI_BROWSER_MEMORY_TAB_SLEEPING_H_
