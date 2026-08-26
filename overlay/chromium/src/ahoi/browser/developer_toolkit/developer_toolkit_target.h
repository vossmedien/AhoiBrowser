// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TARGET_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TARGET_H_

#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace ahoi {

// Developer actions are only meaningful for ordinary web documents. In
// particular, do not expose browser-internal pages or opaque origins to a
// caller that may later inject a document action or change a setting.
bool IsSupportedDeveloperTargetUrl(const GURL& url);
bool IsSupportedDeveloperTarget(const content::WebContents* web_contents);

// Reads the last committed URL, which avoids applying an action to a transient
// omnibox/navigation URL while a document is still changing.
GURL GetSupportedDeveloperTargetUrl(const content::WebContents* web_contents);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TARGET_H_
