// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Ahoi's LocationBarView changes: the copy-URL tooltip carries no menu
// accelerator and the developer master pref updates the compact buttons live
// (split from floating_browser_view_browsertest.cc, source line budget).

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/view.h"

namespace {

constexpr char kAhoiToolkitEnabledPref[] = "ahoi.developer_toolkit.enabled";
constexpr char kAhoiShowCookieButtonPref[] =
    "ahoi.developer_toolbar.show_cookie_button";
constexpr char kAhoiShowCacheButtonPref[] =
    "ahoi.developer_toolbar.show_cache_button";
constexpr char kAhoiShowToolkitButtonPref[] =
    "ahoi.developer_toolbar.show_toolkit_button";

}  // namespace

class LocationBarViewBrowserTest : public InProcessBrowserTest {
 protected:
  LocationBarView* GetLocationBarView() {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    return browser_view->GetLocationBarView();
  }
};

IN_PROC_BROWSER_TEST_F(LocationBarViewBrowserTest,
                       AhoiCopyUrlTooltipHasNoMenuAccelerator) {
  const std::u16string menu_label = l10n_util::GetStringUTF16(IDS_COPY_URL);
  const std::u16string expected_tooltip = gfx::RemoveAccelerator(menu_label);
  bool found_copy_url_tooltip = false;
  for (views::View* const child : GetLocationBarView()->children()) {
    EXPECT_NE(menu_label, child->GetTooltipText());
    found_copy_url_tooltip |= child->GetTooltipText() == expected_tooltip;
  }
  EXPECT_TRUE(found_copy_url_tooltip);
  EXPECT_EQ(std::u16string::npos, expected_tooltip.find(u'&'));
}

IN_PROC_BROWSER_TEST_F(LocationBarViewBrowserTest,
                       AhoiDeveloperMasterPrefUpdatesButtonsLive) {
  LocationBarView* const location_bar = GetLocationBarView();
  ASSERT_TRUE(location_bar);
  const auto find_button = [location_bar](int tooltip_id) -> views::View* {
    const std::u16string tooltip = l10n_util::GetStringUTF16(tooltip_id);
    for (views::View* const child : location_bar->children()) {
      if (child->GetTooltipText() == tooltip) {
        return child;
      }
    }
    return nullptr;
  };
  views::View* const cookie_button =
      find_button(IDS_AHOI_DEVELOPER_COOKIE_BUTTON_TOOLTIP);
  views::View* const cache_button =
      find_button(IDS_AHOI_DEVELOPER_CACHE_BUTTON_TOOLTIP);
  views::View* const toolkit_button =
      find_button(IDS_AHOI_DEVELOPER_HELPERS_BUTTON_TOOLTIP);
  ASSERT_TRUE(cookie_button);
  ASSERT_TRUE(cache_button);
  ASSERT_TRUE(toolkit_button);

  PrefService* const prefs = browser()->GetProfile()->GetPrefs();
  ASSERT_TRUE(prefs->FindPreference(kAhoiToolkitEnabledPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowCookieButtonPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowCacheButtonPref));
  ASSERT_TRUE(prefs->FindPreference(kAhoiShowToolkitButtonPref));
  prefs->ClearPref(kAhoiShowCookieButtonPref);
  prefs->ClearPref(kAhoiShowCacheButtonPref);
  prefs->ClearPref(kAhoiShowToolkitButtonPref);
  ASSERT_FALSE(prefs->GetBoolean(kAhoiShowCookieButtonPref));
  ASSERT_FALSE(prefs->GetBoolean(kAhoiShowCacheButtonPref));
  ASSERT_TRUE(prefs->GetBoolean(kAhoiShowToolkitButtonPref));
  prefs->SetBoolean(kAhoiToolkitEnabledPref, false);
  EXPECT_FALSE(cookie_button->GetVisible());
  EXPECT_FALSE(cache_button->GetVisible());
  EXPECT_FALSE(toolkit_button->GetVisible());

  // This mutates the same live profile and LocationBarView. The registered
  // master-pref callback must reveal the preselected compact action without a
  // browser restart or a second visibility-pref write.
  prefs->SetBoolean(kAhoiToolkitEnabledPref, true);
  EXPECT_FALSE(cookie_button->GetVisible());
  EXPECT_FALSE(cache_button->GetVisible());
  EXPECT_TRUE(toolkit_button->GetVisible());
}
