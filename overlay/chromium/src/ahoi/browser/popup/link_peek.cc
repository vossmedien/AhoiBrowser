// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/popup/link_peek.h"

#include <algorithm>
#include <vector>

#include "ahoi/browser/popup/link_peek_input.h"
#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

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

PeekRequest::PeekRequest() = default;

PeekRequest::PeekRequest(const GURL& url) : url(url) {}

PeekRequest::PeekRequest(const PeekRequest&) = default;

PeekRequest& PeekRequest::operator=(const PeekRequest&) = default;

PeekRequest::~PeekRequest() = default;

// static
PeekRequest PeekRequest::ForTypedUrl(const GURL& url, bool is_search) {
  PeekRequest request(url);
  request.transition = ui::PageTransitionFromInt(
      (is_search ? ui::PAGE_TRANSITION_GENERATED : ui::PAGE_TRANSITION_TYPED) |
      ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
  return request;
}

// static
PeekRequest PeekRequest::FromOpenURLParams(
    const content::OpenURLParams& params) {
  PeekRequest request(params.url);
  request.referrer = params.referrer;
  request.initiator_origin = params.initiator_origin;
  request.transition = params.transition;
  request.started_from_context_menu = params.started_from_context_menu;
  return request;
}

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

bool PeekLink(content::WebContents* opener, const PeekRequest& request) {
  LinkPeekHost* host = FindHost(opener, request.url);
  return host && host->ShowPeek(opener, request);
}

bool CanAutoPeekLink(content::WebContents* opener, const GURL& url) {
  LinkPeekHost* host = FindHost(opener, url);
  return host && host->IsSavedPage(opener);
}

bool LeavesSite(const GURL& page_url, const GURL& link_url) {
  return page_url.SchemeIsHTTPOrHTTPS() && IsPeekableUrl(link_url) &&
         !net::registry_controlled_domains::SameDomainOrHost(
             page_url, link_url,
             net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

bool IsAutoPeekNewWindowCandidate(const NewWindowLink& link) {
  // Background tabs (Cmd-click), new windows (Shift-click) and popups with
  // features keep Chromium's meaning; only the plain click's foreground tab
  // is a candidate.
  return link.disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB &&
         link.user_gesture && !link.has_opener && link.plain_activation &&
         !link.popup_features && LeavesSite(link.source_url, link.target_url);
}

bool ShouldAutoPeekNewWindow(content::WebContents* source,
                             content::WebContents* new_contents,
                             const GURL& target_url,
                             WindowOpenDisposition disposition,
                             const blink::mojom::WindowFeatures& features,
                             bool user_gesture) {
  if (!source || !new_contents ||
      source->GetBrowserContext() != new_contents->GetBrowserContext()) {
    return false;
  }
  NewWindowLink link;
  link.source_url = source->GetLastCommittedURL();
  link.target_url = target_url;
  link.disposition = disposition;
  link.user_gesture = user_gesture;
  link.has_opener = new_contents->HasOpener();
  link.plain_activation = LinkPeekInputTracker::LastInputWasPlain(source);
  link.popup_features =
      features.is_popup || features.has_width || features.has_height;
  return IsAutoPeekNewWindowCandidate(link) &&
         CanAutoPeekLink(source, target_url);
}

std::u16string PeekLinkMenuLabel() {
  return base::i18n::GetConfiguredLocale().starts_with("de")
             ? u"Link in Vorschau öffnen"
             : u"Open link in preview";
}

}  // namespace ahoi::popup
