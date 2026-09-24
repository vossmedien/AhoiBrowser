// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_WEBSITE_SESSION_CONTEXT_H_
#define AHOI_BROWSER_SESSION_WEBSITE_SESSION_CONTEXT_H_

#include <optional>

#include "ahoi/browser/session/session_prefs.h"
#include "content/public/browser/storage_partition_config.h"

class Profile;

namespace content {
class SiteInstance;
class WebContents;
}  // namespace content

namespace ahoi::session {

// Domain and name are native device-local identifiers. Neither is a portable
// Workspace ID nor permitted in a sync payload.
inline constexpr char kWebsiteSessionPartitionDomain[] = "ahoi";
content::StoragePartitionConfig StoragePartitionConfigForWebsiteSession(
    Profile* profile,
    const WebsiteSessionBinding& binding);

// A non-Ahoi non-default partition is intentionally not reinterpreted as a
// workspace session. This keeps Chrome apps, guests and extensions under their
// own native storage authority.
std::optional<WebsiteSessionBinding> WebsiteSessionBindingForSiteInstance(
    Profile* profile,
    content::SiteInstance* site_instance);
std::optional<WebsiteSessionBinding> WebsiteSessionBindingForWebContents(
    Profile* profile,
    content::WebContents* contents);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_WEBSITE_SESSION_CONTEXT_H_
