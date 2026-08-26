// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_POPUP_TYPES_H_
#define AHOI_BROWSER_POPUP_POPUP_TYPES_H_

#include <cstddef>
#include <cstdint>

#include "third_party/blink/public/mojom/window_features/window_features.mojom-forward.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace ahoi::popup {

// Eligibility is deliberately conservative. Security-sensitive flows stay on
// Chromium's native popup-window path instead of being forced into an overlay
// whose browser chrome cannot yet represent every part of their lifecycle.
enum class PopupSafety : uint8_t {
  kSafeForOverlay,
  kNotAWebPopup,
  kInvalidOrUnsupportedScheme,
  kSensitiveAuthenticationFlow,
  kPaymentFlow,
  kPasskeyFlow,
  kFullscreenSizedWindow,
};

enum class PopupFallbackReason : uint8_t {
  kSensitiveAuthenticationFlow,
  kPaymentFlow,
  kPasskeyFlow,
  kFullscreenRequest,
  kUnsupportedNavigation,
  kOpenerClosed,
};

enum class PopupSplitAvailability : uint8_t {
  kAvailable,
  kOpenerMissing,
  kOpenerNotInWindow,
  kSplitFull,
};

PopupSafety ClassifyPopupForOverlay(
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features);

bool IsOverlayEligible(PopupSafety safety);

PopupFallbackReason FallbackReasonForSafety(PopupSafety safety);

// Pure preflight shared by the Chromium adapter and focused unit tests. A
// fifth pane is rejected before the popup WebContents leaves the overlay.
PopupSplitAvailability ClassifySplitAvailability(bool opener_is_live,
                                                 bool opener_is_in_window,
                                                 size_t current_pane_count,
                                                 size_t maximum_pane_count);

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_POPUP_TYPES_H_
