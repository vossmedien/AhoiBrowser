// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/browser_account_policy.h"

#include <memory>

#include "base/command_line.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/account_investigator_factory.h"
#include "chrome/browser/signin/account_reconcilor_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/account_consistency_method.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/sync/base/command_line_switches.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

class BrowserAccountPolicyBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(BrowserAccountPolicyBrowserTest,
                       BrowserAccountAndChromiumSyncStayDisabled) {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  EXPECT_TRUE(command_line->HasSwitch(syncer::kDisableSync));
  EXPECT_EQ("false",
            command_line->GetSwitchValueASCII(kAllowBrowserSigninSwitch));
  EXPECT_FALSE(syncer::IsSyncAllowedByFlag());

  Profile* profile = browser()->GetProfile();
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(prefs::kSigninAllowed));
  EXPECT_FALSE(
      profile->GetPrefs()->GetBoolean(prefs::kSigninAllowedOnNextStartup));
  EXPECT_EQ(signin::AccountConsistencyMethod::kDisabled,
            AccountConsistencyModeManager::GetMethodForProfile(profile));
  EXPECT_EQ(nullptr, AccountInvestigatorFactory::GetForProfile(profile));
  EXPECT_EQ(nullptr, AccountReconcilorFactory::GetForProfile(profile));
  EXPECT_EQ(nullptr, SyncServiceFactory::GetForProfile(profile));
}

}  // namespace
}  // namespace ahoi::privacy

class AhoiManagedBrowserAccountPolicyBrowserTest : public InProcessBrowserTest {
};

IN_PROC_BROWSER_TEST_F(AhoiManagedBrowserAccountPolicyBrowserTest,
                       ManagedSigninPrefsCannotBypassProductPolicy) {
  std::unique_ptr<TestingProfile> managed_profile =
      TestingProfile::Builder().Build();
  managed_profile->GetTestingPrefService()->SetManagedPref(
      prefs::kSigninAllowed, base::Value(true));
  managed_profile->GetTestingPrefService()->SetManagedPref(
      prefs::kSigninAllowedOnNextStartup, base::Value(true));

  EXPECT_TRUE(managed_profile->GetPrefs()->GetBoolean(prefs::kSigninAllowed));
  EXPECT_TRUE(managed_profile->GetPrefs()->GetBoolean(
      prefs::kSigninAllowedOnNextStartup));

  AccountConsistencyModeManager manager(managed_profile.get());
  EXPECT_EQ(signin::AccountConsistencyMethod::kDisabled,
            manager.GetAccountConsistencyMethod());
}
