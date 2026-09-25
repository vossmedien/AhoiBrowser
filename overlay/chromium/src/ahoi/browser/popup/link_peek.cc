// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek.h"

#include <algorithm>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "url/gurl.h"

namespace ahoi::popup {

namespace {

std::vector<LinkPeekHost*>& Hosts() {
  static base::NoDestructor<std::vector<LinkPeekHost*>> hosts;
  return *hosts;
}

LinkPeekHost* FindHost(content::WebContents* opener, const GURL& url) {
  if (!opener || !IsPeekableUrl(url)) {
    return nullptr;
  }
  for (LinkPeekHost* host : Hosts()) {
    if (host->CanPeek(opener, url)) {
      return host;
    }
  }
  return nullptr;
}

}  // namespace

void AddLinkPeekHost(LinkPeekHost* host) {
  if (host && !std::ranges::contains(Hosts(), host)) {
    Hosts().push_back(host);
  }
}

void RemoveLinkPeekHost(LinkPeekHost* host) {
  std::erase(Hosts(), host);
}

bool IsPeekableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

bool CanPeekLink(content::WebContents* opener, const GURL& url) {
  return FindHost(opener, url) != nullptr;
}

bool PeekLink(content::WebContents* opener, const GURL& url) {
  LinkPeekHost* host = FindHost(opener, url);
  return host && host->ShowPeek(opener, url);
}

std::u16string PeekLinkMenuLabel() {
  return base::i18n::GetConfiguredLocale().starts_with("de")
             ? u"Link in Vorschau öffnen"
             : u"Open link in preview";
}

}  // namespace ahoi::popup
