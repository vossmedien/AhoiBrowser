// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_defaults.h"

#include "base/values.h"
#include "chrome/browser/prefetch/pref_names.h"
#include "chrome/common/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_profile_pref_names.h"
#include "components/network_time/network_time_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/privacy_sandbox/privacy_sandbox_prefs.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/variations/client_filterable_state.h"
#include "components/variations/pref_names.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

TEST(PrivacyDefaultsTest, ProfileDefaultsAreSafeAndRemainUserModifiable) {
  TestingPrefServiceSimple profile_prefs;
  profile_prefs.registry()->RegisterBooleanPref(prefs::kHttpsOnlyModeEnabled,
                                                false);
  profile_prefs.registry()->RegisterBooleanPref(prefs::kHttpsFirstBalancedMode,
                                                true);
  profile_prefs.registry()->RegisterBooleanPref(prefs::kSearchSuggestEnabled,
                                                true);
  profile_prefs.registry()->RegisterIntegerPref(
      prefetch::prefs::kNetworkPredictionOptions, 0);
  profile_prefs.registry()->RegisterBooleanPref(prefs::kSafeBrowsingEnabled,
                                                false);
  profile_prefs.registry()->RegisterBooleanPref(prefs::kSafeBrowsingEnhanced,
                                                true);
  profile_prefs.registry()->RegisterBooleanPref(prefs::kSigninAllowed, true);
  profile_prefs.registry()->RegisterBooleanPref(
      prefs::kSigninAllowedOnNextStartup, true);
  profile_prefs.registry()->RegisterBooleanPref(
      metrics::prefs::kAdvancedReportingEnabled, true);
  privacy_sandbox::RegisterProfilePrefs(profile_prefs.registry());

  ApplyProfileDefaults(profile_prefs.registry());

  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kHttpsOnlyModeEnabled));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kHttpsFirstBalancedMode));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kSearchSuggestEnabled));
  EXPECT_EQ(
      2, profile_prefs.GetInteger(prefetch::prefs::kNetworkPredictionOptions));
  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kSafeBrowsingEnabled));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kSafeBrowsingEnhanced));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kSigninAllowed));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kSigninAllowedOnNextStartup));
  EXPECT_FALSE(profile_prefs.GetBoolean(
      metrics::prefs::kAdvancedReportingEnabled));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kPrivacySandboxM1TopicsEnabled));
  EXPECT_FALSE(profile_prefs.GetBoolean(prefs::kPrivacySandboxM1FledgeEnabled));
  EXPECT_FALSE(
      profile_prefs.GetBoolean(prefs::kPrivacySandboxM1AdMeasurementEnabled));
  EXPECT_FALSE(profile_prefs.GetBoolean(
      prefs::kPrivacySandboxRelatedWebsiteSetsEnabled));
  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kPrivacySandboxM1Restricted));

  profile_prefs.SetBoolean(prefs::kSearchSuggestEnabled, true);
  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kSearchSuggestEnabled));
}

TEST(PrivacyDefaultsTest, ProfileDefaultsPreserveHigherPriorityValues) {
  TestingPrefServiceSimple profile_prefs;
  profile_prefs.registry()->RegisterBooleanPref(prefs::kSigninAllowed, true);
  profile_prefs.registry()->RegisterBooleanPref(
      prefs::kSigninAllowedOnNextStartup, true);
  profile_prefs.SetBoolean(prefs::kSigninAllowed, true);
  profile_prefs.SetManagedPref(prefs::kSigninAllowedOnNextStartup,
                               base::Value(true));

  ApplyProfileDefaults(profile_prefs.registry());

  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kSigninAllowed));
  EXPECT_TRUE(profile_prefs.GetBoolean(prefs::kSigninAllowedOnNextStartup));
}

TEST(PrivacyDefaultsTest, LocalStateDisablesReportingAndVariations) {
  TestingPrefServiceSimple local_state;
  local_state.registry()->RegisterBooleanPref(
      network_time::prefs::kNetworkTimeQueriesEnabled, true);
  local_state.registry()->RegisterBooleanPref(
      metrics::prefs::kMetricsReportingEnabled, true);
  local_state.registry()->RegisterIntegerPref(
      variations::prefs::kVariationsRestrictionsByPolicy,
      static_cast<int>(variations::RestrictionPolicy::NO_RESTRICTIONS));

  ApplyLocalStateDefaults(local_state.registry());

  EXPECT_FALSE(
      local_state.GetBoolean(network_time::prefs::kNetworkTimeQueriesEnabled));
  EXPECT_FALSE(
      local_state.GetBoolean(metrics::prefs::kMetricsReportingEnabled));
  EXPECT_EQ(static_cast<int>(variations::RestrictionPolicy::ALL),
            local_state.GetInteger(
                variations::prefs::kVariationsRestrictionsByPolicy));
}

}  // namespace
}  // namespace ahoi::privacy
