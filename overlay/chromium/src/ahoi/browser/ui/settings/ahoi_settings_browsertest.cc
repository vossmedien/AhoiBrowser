// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kToggleQuery[] = R"JS(
  (() => {
    const settingsUi = document.querySelector('settings-ui');
    const settingsMain = settingsUi?.shadowRoot?.querySelector('settings-main');
    const ahoiPage = settingsMain?.shadowRoot?.querySelector(
        '#ahoi.active settings-ahoi-page');
    return ahoiPage?.shadowRoot?.querySelector(
        '#ahoiDeveloperToolkitEnabled') ?? null;
  })()
)JS";

class AhoiSettingsBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(AhoiSettingsBrowserTest,
                       AhoiRouteRoundTripsLiveProfilePref) {
  PrefService* const prefs = browser()->GetProfile()->GetPrefs();
  ASSERT_TRUE(
      prefs->FindPreference(ahoi::developer_toolkit_prefs::kToolkitEnabled));
  prefs->SetBoolean(ahoi::developer_toolkit_prefs::kToolkitEnabled, false);

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), chrome::GetSettingsUrl("ahoi")));
  content::WebContents* const web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  ASSERT_TRUE(content::EvalJs(web_contents, R"JS(
    (async () => {
      await customElements.whenDefined('settings-ui');
      await customElements.whenDefined('settings-ahoi-page');
      const settingsUi = document.querySelector('settings-ui');
      const settingsMain =
          settingsUi?.shadowRoot?.querySelector('settings-main');
      if (!settingsMain) {
        return false;
      }
      await settingsMain.whenViewSwitchingDone();
      const ahoiPage = settingsMain.shadowRoot.querySelector(
          '#ahoi.active settings-ahoi-page');
      const toggle = ahoiPage?.shadowRoot?.querySelector(
          '#ahoiDeveloperToolkitEnabled');
      if (!toggle || toggle.checked) {
        return false;
      }
      toggle.click();
      return true;
    })()
  )JS")
                  .ExtractBool());

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return prefs->GetBoolean(ahoi::developer_toolkit_prefs::kToolkitEnabled);
  }));
  EXPECT_TRUE(
      content::EvalJs(web_contents, std::string(kToggleQuery) + ".checked")
          .ExtractBool());

  prefs->SetBoolean(ahoi::developer_toolkit_prefs::kToolkitEnabled, false);
  EXPECT_TRUE(content::EvalJs(web_contents, std::string(R"JS(
    (async () => {
      const toggle =
  )JS") + kToggleQuery + R"JS(;
      if (!toggle) {
        return false;
      }
      for (let frame = 0; frame < 120 && toggle.checked; ++frame) {
        await new Promise(resolve => requestAnimationFrame(resolve));
      }
      return !toggle.checked;
    })()
  )JS")
                  .ExtractBool());
}

}  // namespace
