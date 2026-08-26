// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"

#include "content/public/browser/web_contents.h"

namespace ahoi {

bool IsSupportedDeveloperTargetUrl(const GURL& url) {
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS()) &&
         !url::Origin::Create(url).opaque();
}

bool IsSupportedDeveloperTarget(const content::WebContents* web_contents) {
  return web_contents &&
         IsSupportedDeveloperTargetUrl(web_contents->GetLastCommittedURL());
}

GURL GetSupportedDeveloperTargetUrl(const content::WebContents* web_contents) {
  if (!IsSupportedDeveloperTarget(web_contents)) {
    return GURL();
  }
  return web_contents->GetLastCommittedURL();
}

}  // namespace ahoi
