// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/command_bar/quick_window.h"
#include "chrome/app/chrome_command_ids.h"
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
  if (target->GetWindow()) {
    target->GetWindow()->Show();
    target->GetWindow()->Activate();
  }
  // Detaching the final popup tab follows Chromium's normal empty-window
  // lifecycle. The WebContents and its renderer/navigation state stay intact.
  return true;
}

}  // namespace ahoi::quick_window
