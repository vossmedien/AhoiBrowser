// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_POPUP_LINK_PEEK_THROTTLE_H_
#define AHOI_BROWSER_POPUP_LINK_PEEK_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace ahoi::popup {

// Automatic Peek (off by default): a user's link click on a saved page that
// leaves for another site is cancelled before its request and shown as a
// preview instead, so the saved page stays where it was. Everything else –
// same-site links, forms, scripts, other windows, pages that are not saved –
// navigates normally.
class LinkPeekNavigationThrottle final : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit LinkPeekNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  LinkPeekNavigationThrottle(const LinkPeekNavigationThrottle&) = delete;
  LinkPeekNavigationThrottle& operator=(const LinkPeekNavigationThrottle&) =
      delete;
  ~LinkPeekNavigationThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;
};

}  // namespace ahoi::popup

#endif  // AHOI_BROWSER_POPUP_LINK_PEEK_THROTTLE_H_
