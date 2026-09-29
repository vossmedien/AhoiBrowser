// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Link-Peek entry points of PopupOverlayController: previews of a link in a
// new WebContents and automatic Peek of a saved page's target=_blank click.

#include "ahoi/browser/ui/popup/popup_overlay_controller.h"

#include <memory>
#include <optional>

#include "ahoi/browser/popup/link_peek.h"
#include "ahoi/browser/popup/popup_types.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "url/gurl.h"

namespace ahoi::popup_ui {

bool PopupOverlayController::CanPeek(content::WebContents* opener,
                                     const GURL& url) {
  return popup::IsPeekableUrl(url) && CanHostFor(opener);
}

bool PopupOverlayController::ShowPeek(content::WebContents* opener,
                                      const popup::PeekRequest& request) {
  const GURL& url = request.url;
  if (!CanPeek(opener, url) || !opener->GetPrimaryMainFrame()) {
    return false;
  }
  // The preview stays in the opener's website session (ADR 0011), so it
  // sees the same logins and never another Workspace's cookies.
  content::BrowserContext* context = opener->GetBrowserContext();
  content::StoragePartition* partition = context->GetStoragePartition(
      opener->GetPrimaryMainFrame()->GetSiteInstance());
  if (!partition) {
    return false;
  }
  const content::StoragePartitionConfig& config = partition->GetConfig();
  content::WebContents::CreateParams params(
      context, config.is_default()
                   ? content::SiteInstance::CreateForURL(context, url)
                   : content::SiteInstance::CreateForFixedStoragePartition(
                         context, url, config));
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  content::WebContents* peek = contents.get();
  if (!AdoptAndShow(opener, &contents, blink::mojom::WindowFeatures(),
                    /*user_gesture=*/true)) {
    return false;
  }
  // The intercepted request as the page sent it; the opener's own URL and
  // origin are never substituted. Sanitizing again under the request's own
  // policy keeps rel=noreferrer and Referrer-Policy: no-referrer silent.
  content::NavigationController::LoadURLParams load(url);
  load.transition_type = request.transition;
  load.initiator_origin = request.initiator_origin;
  load.referrer = content::Referrer::SanitizeForRequest(url, request.referrer);
  load.started_from_context_menu = request.started_from_context_menu;
  peek->GetController().LoadURLWithParams(load);
  return true;
}

bool PopupOverlayController::TryAutoPeekNewWindow(
    content::WebContents* source,
    std::unique_ptr<content::WebContents>* new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture) {
  if (!new_contents || !*new_contents || !browser_ ||
      !browser_->GetProfile()->GetPrefs()->GetBoolean(
          popup::kAutoPeekFromSavedPagesPref) ||
      !CanPeek(source, target_url) ||
      !popup::ShouldAutoPeekNewWindow(source, new_contents->get(), target_url,
                                      disposition, window_features,
                                      user_gesture)) {
    return false;
  }
  // Sign-in, payment and passkey pages would leave the overlay again right
  // after their commit; they keep Chromium's tab from the start.
  if (!popup::IsOverlayEligible(popup::ClassifyPopupForOverlay(
          target_url, WindowOpenDisposition::NEW_POPUP,
          blink::mojom::WindowFeatures()))) {
    return false;
  }
  // Content has not started loading `new_contents` yet; it loads the link,
  // with its referrer and initiator, into the adopted WebContents once this
  // returns, so the page is requested exactly once and keeps its noopener
  // browsing context. A later fallback returns it to the tab Chromium would
  // have opened, not to a popup window.
  if (!AdoptAndShow(source, new_contents, blink::mojom::WindowFeatures(),
                    user_gesture)) {
    return false;
  }
  fallback_disposition_ = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  return true;
}

bool PopupOverlayController::IsSavedPage(content::WebContents* contents) {
  tabs::TabInterface* const tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  SessionBridge* const bridge =
      browser_ ? SessionBridgeFactory::GetForProfile(browser_->GetProfile())
               : nullptr;
  if (!tab || !bridge || !bridge->tab_tree_store()) {
    return false;
  }
  const std::optional<session::TabSessionMetadata> metadata =
      bridge->GetTabSessionMetadata(tab);
  // A temporary tab is bound to a temporary tree row too; only a page the
  // user saved counts.
  tab_tree::TreeNode node;
  return metadata && metadata->tree_node_id.has_value() &&
         bridge->tab_tree_store()->GetNode(*metadata->tree_node_id, &node) ==
             tab_tree::TabTreeStore::Result::kOk &&
         !node.is_temporary;
}

}  // namespace ahoi::popup_ui
