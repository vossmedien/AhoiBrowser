// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_LINK_ROUTING_DISPATCH_H_
#define AHOI_BROWSER_NAVIGATION_LINK_ROUTING_DISPATCH_H_

#include <vector>

#include "ahoi/browser/navigation/link_routing.h"
#include "base/functional/callback.h"
#include "url/gurl.h"

namespace ahoi::navigation {

// Receives URLs that routing accepted but could not open (main Profile not
// loadable, routing found disabled after the Profile loaded, no target window
// could be obtained). The caller opens them with Chromium's default path.
using ExternalUrlFallback =
    base::RepeatingCallback<void(std::vector<GURL> urls)>;

// Routes external links (Launch Services / startup URLs) before their first
// navigation, using the main Profile's `ahoi.navigation.link_routing`
// settings. Only IsRoutableUrl() URLs are accepted; they are deduplicated in
// order. A target Workspace of the main Profile is activated in a main window
// before a new foreground tab opens there, so the tab's website session is
// fixed by the Workspace; a fully separated Workspace's Profile window is
// presented first. Quick Window mode opens an ephemeral popup of the target
// Profile. Never opens incognito. When the winning explicit target no longer
// exists, a window-modal chooser (link_routing_target_chooser.h) asks for a
// Workspace; only its checked "Für diese Website merken" writes an exact-host
// rule (RememberSiteChoice), and cancelling does not open the link.
//
// Returns false, without calling `fallback`, when nothing was taken over:
// no routable URL, no Profile manager, or routing is disabled in the already
// loaded main Profile. Returns true when the URLs are now owned by routing.
bool RouteExternalUrls(const std::vector<GURL>& urls,
                       ExternalUrlFallback fallback);

}  // namespace ahoi::navigation

#endif  // AHOI_BROWSER_NAVIGATION_LINK_ROUTING_DISPATCH_H_
