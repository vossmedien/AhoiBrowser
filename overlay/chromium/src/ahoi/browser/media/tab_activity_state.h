// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_MEDIA_TAB_ACTIVITY_STATE_H_
#define AHOI_BROWSER_MEDIA_TAB_ACTIVITY_STATE_H_

#include <optional>

#include "components/tabs/public/tab_alert.h"

namespace ahoi {

// Capture activity projected from Chromium's TabAlertController. This model
// intentionally does not query permissions or capture devices itself: the
// controller remains the single source of truth for WebRTC, camera,
// microphone, tab capture, and desktop capture lifecycle.
struct AhoiTabActivityState {
  std::optional<tabs::TabAlert> primary_activity;

  static AhoiTabActivityState FromChromiumAlert(
      std::optional<tabs::TabAlert> alert);

  bool operator==(const AhoiTabActivityState&) const = default;
};

bool IsAhoiCaptureActivityAlert(tabs::TabAlert alert);

}  // namespace ahoi

#endif  // AHOI_BROWSER_MEDIA_TAB_ACTIVITY_STATE_H_
