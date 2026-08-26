// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/privacy_defaults.h"

#include <string_view>
#include <utility>

#include "base/values.h"
#include "chrome/browser/prefetch/pref_names.h"
#include "chrome/common/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_reporting_level.h"
#include "components/network_time/network_time_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/privacy_sandbox/privacy_sandbox_prefs.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/variations/client_filterable_state.h"
#include "components/variations/pref_names.h"

namespace ahoi::privacy {
namespace {

// This is a persisted policy value. Keep aligned with
// prefetch::NetworkPredictionOptions::kDisabled.
constexpr int kNetworkPredictionDisabled = 2;

void SetDefaultIfRegistered(PrefRegistrySimple* registry,
                            std::string_view name,
                            base::Value value) {
  if (registry && registry->GetRegisteredPrefType(name).has_value()) {
    registry->SetDefaultPrefValue(name, std::move(value));
  }
}

}  // namespace

void ApplyProfileDefaults(PrefRegistrySimple* registry) {
  SetDefaultIfRegistered(registry, prefs::kHttpsOnlyModeEnabled,
                         base::Value(true));
  SetDefaultIfRegistered(registry, prefs::kHttpsFirstBalancedMode,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kSearchSuggestEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefetch::prefs::kNetworkPredictionOptions,
                         base::Value(kNetworkPredictionDisabled));
  SetDefaultIfRegistered(registry, prefs::kSafeBrowsingEnabled,
                         base::Value(true));
  SetDefaultIfRegistered(registry, prefs::kSafeBrowsingEnhanced,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kSigninAllowed, base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kSigninAllowedOnNextStartup,
                         base::Value(false));

  SetDefaultIfRegistered(registry, prefs::kPrivacySandboxM1TopicsEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kPrivacySandboxM1FledgeEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kPrivacySandboxM1AdMeasurementEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry,
                         prefs::kPrivacySandboxRelatedWebsiteSetsEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry, prefs::kPrivacySandboxM1Restricted,
                         base::Value(true));
}

void ApplyLocalStateDefaults(PrefRegistrySimple* registry) {
  SetDefaultIfRegistered(registry,
                         network_time::prefs::kNetworkTimeQueriesEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(registry, metrics::prefs::kMetricsReportingEnabled,
                         base::Value(false));
  SetDefaultIfRegistered(
      registry, metrics::prefs::kMetricsReportingLevel,
      base::Value(static_cast<int>(metrics::MetricsReportingLevel::kNone)));
  SetDefaultIfRegistered(
      registry, variations::prefs::kVariationsRestrictionsByPolicy,
      base::Value(static_cast<int>(variations::RestrictionPolicy::ALL)));
}

}  // namespace ahoi::privacy
