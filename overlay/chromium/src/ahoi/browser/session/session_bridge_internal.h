// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_BRIDGE_INTERNAL_H_
#define AHOI_BROWSER_SESSION_SESSION_BRIDGE_INTERNAL_H_

#include <string>

#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

namespace ahoi::session_internal {

GURL GetRuntimeTabUrl(content::WebContents* contents);
std::u16string GetRuntimeTabTitle(tabs::TabInterface* tab, const GURL& url);

}  // namespace ahoi::session_internal

#endif  // AHOI_BROWSER_SESSION_SESSION_BRIDGE_INTERNAL_H_
