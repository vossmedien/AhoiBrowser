// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/glic/test_support/glic_test_environment.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/web_ui_mocha_browser_test.h"
#include "content/public/test/browser_test.h"

class AhoiAppearanceSettingsBrowserTest : public WebUIMochaBrowserTest {
 protected:
  AhoiAppearanceSettingsBrowserTest() {
    scoped_feature_list_.InitAndDisableFeature(
        features::kGlicDefaultTabContextSetting);
    set_test_loader_host(chrome::kChromeUISettingsHost);
  }

 private:
  glic::GlicTestEnvironment glic_test_environment_{
      {.force_signin_and_glic_capability = false}};
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(AhoiAppearanceSettingsBrowserTest, AppearancePage) {
  RunTest("settings/appearance_page_test.js", "mocha.run()");
}
