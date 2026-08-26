// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/popup_types.h"

#include <initializer_list>
#include <string>
#include <string_view>

#include "base/strings/string_util.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "url/url_constants.h"

namespace ahoi::popup {

namespace {

bool ContainsAny(std::string_view value,
                 std::initializer_list<std::string_view> needles) {
  for (const std::string_view needle : needles) {
    if (value.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

PopupSafety ClassifyPopupForOverlay(
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features) {
  if (disposition != WindowOpenDisposition::NEW_POPUP) {
    return PopupSafety::kNotAWebPopup;
  }

  // about:blank is the normal bootstrap URL for script-created windows. The
  // committed destination is classified again by PopupOverlayService.
  const bool is_about_blank =
      target_url == GURL(url::kAboutBlankURL) || target_url.is_empty();
  if (!is_about_blank && !target_url.SchemeIsHTTPOrHTTPS()) {
    return PopupSafety::kInvalidOrUnsupportedScheme;
  }

  const std::string host = base::ToLowerASCII(target_url.host());
  const std::string path = base::ToLowerASCII(target_url.path());
  const std::string query = base::ToLowerASCII(target_url.query());
  if (ContainsAny(host, {"oauth", "openid", "saml", "accounts."}) ||
      ContainsAny(path, {"/oauth", "/authorize", "/sso", "/signin", "/sign-in",
                         "/login"}) ||
      ContainsAny(query, {"response_type=", "code_challenge=", "saml"})) {
    return PopupSafety::kSensitiveAuthenticationFlow;
  }
  if (ContainsAny(host, {"payment", "checkout", "billing"}) ||
      ContainsAny(path, {"/payment", "/checkout", "/billing"})) {
    return PopupSafety::kPaymentFlow;
  }
  if (ContainsAny(host, {"passkey", "webauthn"}) ||
      ContainsAny(path, {"/passkey", "/webauthn", "/credential"})) {
    return PopupSafety::kPasskeyFlow;
  }

  // Explicitly application-sized popups stay native. Small size hints are
  // treated as a presentation preference, never a security boundary.
  if (window_features.has_width && window_features.has_height &&
      window_features.bounds.width() >= 1000 &&
      window_features.bounds.height() >= 700) {
    return PopupSafety::kFullscreenSizedWindow;
  }

  return PopupSafety::kSafeForOverlay;
}

bool IsOverlayEligible(PopupSafety safety) {
  return safety == PopupSafety::kSafeForOverlay;
}

PopupFallbackReason FallbackReasonForSafety(PopupSafety safety) {
  switch (safety) {
    case PopupSafety::kSensitiveAuthenticationFlow:
      return PopupFallbackReason::kSensitiveAuthenticationFlow;
    case PopupSafety::kPaymentFlow:
      return PopupFallbackReason::kPaymentFlow;
    case PopupSafety::kPasskeyFlow:
      return PopupFallbackReason::kPasskeyFlow;
    case PopupSafety::kFullscreenSizedWindow:
      return PopupFallbackReason::kFullscreenRequest;
    case PopupSafety::kSafeForOverlay:
    case PopupSafety::kNotAWebPopup:
    case PopupSafety::kInvalidOrUnsupportedScheme:
      return PopupFallbackReason::kUnsupportedNavigation;
  }
  return PopupFallbackReason::kUnsupportedNavigation;
}

PopupSplitAvailability ClassifySplitAvailability(bool opener_is_live,
                                                 bool opener_is_in_window,
                                                 size_t current_pane_count,
                                                 size_t maximum_pane_count) {
  if (!opener_is_live) {
    return PopupSplitAvailability::kOpenerMissing;
  }
  if (!opener_is_in_window) {
    return PopupSplitAvailability::kOpenerNotInWindow;
  }
  if (current_pane_count >= maximum_pane_count) {
    return PopupSplitAvailability::kSplitFull;
  }
  return PopupSplitAvailability::kAvailable;
}

}  // namespace ahoi::popup
