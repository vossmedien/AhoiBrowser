// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>

#include "ahoi/browser/developer_toolkit/developer_toolkit_prefs.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/settings_private/prefs_util.h"
#include "chrome/browser/extensions/api/settings_private/prefs_util_enums.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {
namespace {

class AhoiSettingsPrivatePrefsTest : public testing::Test {
 public:
  AhoiSettingsPrivatePrefsTest()
      : profile_manager_(TestingBrowserProcess::GetGlobal()) {}

  void SetUp() override {
    ASSERT_TRUE(profile_manager_.SetUp());
    profile_ = profile_manager_.CreateTestingProfile("test_profile");
    prefs_util_ = std::make_unique<PrefsUtil>(profile_);
  }

  void TearDown() override {
    prefs_util_.reset();
    profile_ = nullptr;
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfileManager profile_manager_;
  raw_ptr<TestingProfile> profile_ = nullptr;
  std::unique_ptr<PrefsUtil> prefs_util_;
};

TEST_F(AhoiSettingsPrivatePrefsTest,
       AhoiSettingsPrefsAreAllowlistedWithStableTypes) {
  const struct {
    const char* name;
    api::settings_private::PrefType type;
  } expectations[] = {
      {ahoi::appearance::kGlassEnabledPref,
       api::settings_private::PrefType::kBoolean},
      {ahoi::appearance::kFloatingNavigationAutoHideEnabledPref,
       api::settings_private::PrefType::kBoolean},
      {ahoi::appearance::kFloatingNavigationRevealNotchEnabledPref,
       api::settings_private::PrefType::kBoolean},
      {ahoi::appearance::kFloatingNavigationAutoHideDelayMsPref,
       api::settings_private::PrefType::kNumber},
      {ahoi::sync::kSyncEnabledPref, api::settings_private::PrefType::kBoolean},
      {ahoi::developer_toolkit_prefs::kToolkitEnabled,
       api::settings_private::PrefType::kBoolean},
      {ahoi::developer_toolkit_prefs::kShowCookieButton,
       api::settings_private::PrefType::kBoolean},
      {ahoi::developer_toolkit_prefs::kShowCacheButton,
       api::settings_private::PrefType::kBoolean},
      {ahoi::developer_toolkit_prefs::kShowToolkitButton,
       api::settings_private::PrefType::kBoolean},
  };

  for (const auto& expectation : expectations) {
    EXPECT_EQ(expectation.type,
              prefs_util_->GetAllowlistedPrefType(expectation.name))
        << expectation.name;
    std::optional<api::settings_private::PrefObject> pref =
        prefs_util_->GetPref(expectation.name);
    ASSERT_TRUE(pref.has_value()) << expectation.name;
    EXPECT_EQ(expectation.type, pref->type) << expectation.name;
  }

  for (const char* writable_boolean_pref : {
           ahoi::appearance::kGlassEnabledPref,
           ahoi::appearance::kFloatingNavigationAutoHideEnabledPref,
           ahoi::appearance::kFloatingNavigationRevealNotchEnabledPref,
           ahoi::sync::kSyncEnabledPref,
           ahoi::developer_toolkit_prefs::kToolkitEnabled,
           ahoi::developer_toolkit_prefs::kShowCookieButton,
           ahoi::developer_toolkit_prefs::kShowCacheButton,
           ahoi::developer_toolkit_prefs::kShowToolkitButton,
       }) {
    const bool next_value =
        !profile_->GetPrefs()->GetBoolean(writable_boolean_pref);
    base::Value value(next_value);
    EXPECT_EQ(settings_private::SetPrefResult::SUCCESS,
              prefs_util_->SetPref(writable_boolean_pref, &value))
        << writable_boolean_pref;
    EXPECT_EQ(next_value,
              profile_->GetPrefs()->GetBoolean(writable_boolean_pref))
        << writable_boolean_pref;
  }

  constexpr int kAutoHideDelayMs = 701;
  base::Value delay(kAutoHideDelayMs);
  EXPECT_EQ(
      settings_private::SetPrefResult::SUCCESS,
      prefs_util_->SetPref(
          ahoi::appearance::kFloatingNavigationAutoHideDelayMsPref, &delay));
  EXPECT_EQ(kAutoHideDelayMs,
            profile_->GetPrefs()->GetInteger(
                ahoi::appearance::kFloatingNavigationAutoHideDelayMsPref));
}

TEST_F(AhoiSettingsPrivatePrefsTest, AhoiStartupModeIsAllowlistedAndWritable) {
  std::optional<api::settings_private::PrefObject> pref =
      prefs_util_->GetPref(ahoi::session::kStartupModePref);
  ASSERT_TRUE(pref.has_value());
  EXPECT_EQ(api::settings_private::PrefType::kString, pref->type);

  base::Value continue_value("continue");
  EXPECT_EQ(
      settings_private::SetPrefResult::SUCCESS,
      prefs_util_->SetPref(ahoi::session::kStartupModePref, &continue_value));
  EXPECT_EQ(ahoi::session::StartupMode::kContinue,
            ahoi::session::GetStartupMode(*profile_->GetPrefs()));
}

}  // namespace
}  // namespace extensions
