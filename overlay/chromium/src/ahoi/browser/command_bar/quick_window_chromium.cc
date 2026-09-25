// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/command_bar/quick_window.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "chrome/app/chrome_command_ids.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/window_open_disposition.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace ahoi::quick_window {
namespace {

bool IsEligibleProfile(const Profile* profile) {
  return profile && profile->IsRegularProfile() && !profile->IsOffTheRecord();
}

}  // namespace

Browser* CreateAndShowQuickWindow(Profile* profile,
                                  const gfx::Rect& anchor_bounds) {
  if (!IsEligibleProfile(profile)) {
    return nullptr;
  }

  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_POPUP, profile,
                                   /*from_user_gesture=*/true);
  params.is_trusted_source = true;
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  params.initial_bounds = CalculateQuickWindowBounds(anchor_bounds);
  params.initial_origin_specified =
      BrowserWindowCreateParams::ValueSpecified::kSpecified;
  params.user_title = l10n_util::GetStringUTF8(IDS_AHOI_QUICK_WINDOW_TITLE);
  BrowserWindowInterface* const quick_window =
      CreateBrowserWindow(std::move(params));
  Browser* const quick_browser =
      quick_window ? quick_window->GetBrowserForMigrationOnly() : nullptr;
  if (!quick_browser) {
    return nullptr;
  }

  if (!chrome::AddAndReturnTabAt(quick_browser, GURL(url::kAboutBlankURL),
                                 /*index=*/-1, /*foreground=*/true)) {
    if (quick_browser->GetWindow()) {
      quick_browser->GetWindow()->Close();
    }
    return nullptr;
  }
  if (quick_browser->GetWindow()) {
    quick_browser->GetWindow()->Show();
  }
  BrowserView* const quick_view =
      BrowserView::GetBrowserViewForBrowser(quick_browser);
  if (!quick_view || !quick_view->ShowAhoiCommandBar(IDC_FOCUS_LOCATION)) {
    chrome::FocusLocationBar(quick_browser);
  }
  return quick_browser;
}

bool CanMoveActiveTabToNormalWindow(const Browser* popup_browser) {
  return popup_browser &&
         popup_browser->GetType() == BrowserWindowInterface::TYPE_POPUP &&
         IsEligibleProfile(popup_browser->GetProfile()) &&
         popup_browser->GetTabStripModel() &&
         popup_browser->GetTabStripModel()->active_index() >= 0;
}

namespace {

// ADR 0011 step 2 (handoff 050): the adopting window can be hidden behind a
// window of another Profile that a hand-over presented in its frame (for
// example a fully separated Workspace), or be newly created. Showing it
// directly would put two windows over one frame; the regular hand-over
// presents it in the presented window's frame and hides that one instead.
void ShowAdoptingWindow(Browser* target) {
  ui::BaseWindow* const window = target->GetWindow();
  if (!window) {
    return;
  }
  if (!window->IsVisible() && !window->IsMinimized()) {
    BrowserWindowInterface* presented = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [target, &presented](BrowserWindowInterface* candidate) {
          ui::BaseWindow* const candidate_window = candidate->GetWindow();
          if (candidate->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              candidate->GetProfile() != target->GetProfile() &&
              candidate_window && candidate_window->IsVisible() &&
              !candidate_window->IsMinimized()) {
            presented = candidate;
            return false;
          }
          return true;
        });
    if (presented) {
      session::PresentProfileWindow(target->GetProfile(), presented,
                                    base::DoNothing());
      return;
    }
  }
  window->Show();
  window->Activate();
}

}  // namespace

bool MoveActiveTabToNormalWindow(Browser* popup_browser) {
  if (!CanMoveActiveTabToNormalWindow(popup_browser)) {
    return false;
  }

  Browser* target = nullptr;
  ForEachCurrentAndNewBrowserWindowInterfaceOrderedByActivation(
      [popup_browser, &target](BrowserWindowInterface* candidate) {
        if (candidate != popup_browser &&
            candidate->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            candidate->GetProfile() == popup_browser->GetProfile()) {
          target = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  const bool created_target = !target;
  if (!target) {
    BrowserWindowInterface* const target_window = CreateBrowserWindow(
        BrowserWindowCreateParams(popup_browser->GetProfile(),
                                  /*from_user_gesture=*/true));
    target = target_window ? target_window->GetBrowserForMigrationOnly()
                           : nullptr;
  }
  if (!target) {
    return false;
  }

  TabStripModel* const source_model = popup_browser->GetTabStripModel();

  // Handoff 011 S7 and ADR 0011: a Quick Window page runs in the Profile's
  // shared website session. Moving that WebContents into a Workspace with its
  // own website sessions would mix two accounts, so the page is reopened
  // there instead and loads in that Workspace's own session.
  SessionBridge* const bridge =
      SessionBridgeFactory::GetForProfile(target->GetProfile());
  const std::optional<base::Uuid> target_workspace =
      bridge ? bridge->GetActiveWorkspaceForWindow(target) : std::nullopt;
  if (target_workspace.has_value() &&
      bridge->HasOwnWebsiteSessions(*target_workspace)) {
    content::WebContents* const contents =
        source_model->GetActiveWebContents();
    const GURL url = contents ? contents->GetLastCommittedURL() : GURL();
    if (!url.is_valid() || url.IsAboutBlank()) {
      if (created_target && target->GetWindow()) {
        target->GetWindow()->Close();
      }
      return false;
    }
    target->OpenGURL(url, WindowOpenDisposition::NEW_FOREGROUND_TAB);
    ShowAdoptingWindow(target);
    source_model->CloseWebContentsAt(source_model->active_index(),
                                     TabCloseTypes::CLOSE_USER_GESTURE);
    return true;
  }

  std::unique_ptr<tabs::TabModel> tab =
      source_model->DetachTabAtForInsertion(source_model->active_index());
  if (!tab) {
    if (created_target && target->GetWindow()) {
      target->GetWindow()->Close();
    }
    return false;
  }

  target->GetTabStripModel()->InsertDetachedTabAt(
      target->GetTabStripModel()->count(), std::move(tab),
      AddTabTypes::ADD_ACTIVE);
  ShowAdoptingWindow(target);
  // Detaching the final popup tab follows Chromium's normal empty-window
  // lifecycle. The WebContents and its renderer/navigation state stay intact.
  return true;
}

}  // namespace ahoi::quick_window
