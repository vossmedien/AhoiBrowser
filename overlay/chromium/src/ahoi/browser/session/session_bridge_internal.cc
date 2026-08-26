// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge_internal.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi::session_internal {

GURL GetRuntimeTabUrl(content::WebContents* contents) {
  if (!contents) {
    return GURL(chrome::kChromeUINewTabURL);
  }
  // Persist only committed destinations. A pending visible URL may still fail
  // or be cancelled and must not replace the durable saved-page target.
  GURL url = contents->GetLastCommittedURL();
  if (!url.is_valid() || url.is_empty()) {
    url = contents->GetVisibleURL();
  }
  return url.is_valid() && !url.is_empty() ? url
                                           : GURL(chrome::kChromeUINewTabURL);
}

std::u16string GetRuntimeTabTitle(tabs::TabInterface* tab, const GURL& url) {
  std::u16string title = tab ? tab->GetTitle() : std::u16string();
  if (!title.empty()) {
    return title;
  }
  title = base::UTF8ToUTF16(url.host());
  return title.empty() ? u"Ahoi" : title;
}

}  // namespace ahoi::session_internal
