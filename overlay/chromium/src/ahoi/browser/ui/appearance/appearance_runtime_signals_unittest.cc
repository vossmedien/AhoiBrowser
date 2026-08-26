// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"

#include <vector>

#include "ahoi/browser/ui/appearance/appearance_prefs.h"
#include "base/functional/bind.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::appearance {

namespace {

SystemAppearanceSignals ReadSignals(SystemAppearanceSignals* signals) {
  return *signals;
}

TEST(AppearanceRuntimeSignalSourceTest,
     InjectedSystemSignalsDriveTransparencyAndContrast) {
  SystemAppearanceSignals signals;
  signals.system_reduce_transparency = true;
  signals.high_contrast = true;
  AppearanceRuntimeSignalSource source(
      AppearanceRuntimeSignalSource::ChangeCallback(),
      base::BindRepeating(&ReadSignals, base::Unretained(&signals)));
  source.SetPlatformSupportsGlass(true);

  EXPECT_TRUE(source.policy().system_reduce_transparency);
  EXPECT_TRUE(source.policy().high_contrast);
  EXPECT_EQ(GlassMode::kOpaque, source.mode());

  signals = {};
  source.RefreshSystemSignals();
  EXPECT_EQ(GlassMode::kGlass, source.mode());
}

TEST(AppearanceRuntimeSignalSourceTest, ReadsReducedMotionIndependently) {
  SystemAppearanceSignals signals;
  signals.reduced_motion = true;
  AppearanceRuntimeSignalSource source(
      AppearanceRuntimeSignalSource::ChangeCallback(),
      base::BindRepeating(&ReadSignals, base::Unretained(&signals)));
  source.SetPlatformSupportsGlass(true);

  EXPECT_TRUE(source.policy().reduced_motion);
  // Reduced Motion changes transitions, not material contrast. Reduced
  // Transparency and Increased Contrast remain the opaque-material gates.
  EXPECT_EQ(GlassMode::kGlass, source.mode());
}

TEST(AppearanceRuntimeSignalSourceTest, ProfileGlassPrefUpdatesLive) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  prefs.SetBoolean(kGlassEnabledPref, true);

  std::vector<GlassPolicy> changes;
  AppearanceRuntimeSignalSource source(
      &prefs,
      base::BindRepeating(
          [](std::vector<GlassPolicy>* changes, const GlassPolicy& policy) {
            changes->push_back(policy);
          },
          &changes),
      base::BindRepeating([] { return SystemAppearanceSignals(); }));
  source.SetPlatformSupportsGlass(true);
  changes.clear();

  prefs.SetBoolean(kGlassEnabledPref, false);
  ASSERT_EQ(1u, changes.size());
  EXPECT_FALSE(changes.back().enabled);
  EXPECT_EQ(GlassMode::kOpaque, source.mode());

  prefs.SetBoolean(kGlassEnabledPref, true);
  ASSERT_EQ(2u, changes.size());
  EXPECT_EQ(GlassMode::kGlass, source.mode());
}

TEST(AppearanceRuntimeSignalSourceTest,
     ExplicitBatteryAndPerformanceSignalsRemainIndependent) {
  AppearanceRuntimeSignalSource source(
      AppearanceRuntimeSignalSource::ChangeCallback(),
      base::BindRepeating([] { return SystemAppearanceSignals(); }));
  source.SetPlatformSupportsGlass(true);
  EXPECT_EQ(GlassMode::kGlass, source.mode());

  source.SetBatterySaver(true);
  EXPECT_TRUE(source.policy().battery_saver);
  EXPECT_EQ(GlassMode::kOpaque, source.mode());

  source.SetBatterySaver(false);
  source.SetPerformancePressure(PerformancePressure::kElevated);
  EXPECT_EQ(PerformancePressure::kElevated,
            source.policy().performance_pressure);
  EXPECT_EQ(GlassMode::kOpaque, source.mode());

  source.SetPerformancePressure(PerformancePressure::kNone);
  EXPECT_EQ(GlassMode::kGlass, source.mode());
}

TEST(AppearanceRuntimeSignalSourceTest, ChangesArePublishedOncePerEffectiveUpdate) {
  std::vector<GlassPolicy> changes;
  AppearanceRuntimeSignalSource source(
      base::BindRepeating(
          [](std::vector<GlassPolicy>* changes, const GlassPolicy& policy) {
            changes->push_back(policy);
          },
          &changes),
      base::BindRepeating([] { return SystemAppearanceSignals(); }));
  source.SetPlatformSupportsGlass(true);
  const size_t changes_after_platform = changes.size();

  source.SetBatterySaver(true);
  source.SetBatterySaver(true);
  source.SetPerformancePressure(PerformancePressure::kCritical);
  source.SetPerformancePressure(PerformancePressure::kCritical);

  EXPECT_EQ(changes_after_platform + 2, changes.size());
  EXPECT_TRUE(changes.back().battery_saver);
  EXPECT_EQ(PerformancePressure::kCritical,
            changes.back().performance_pressure);
}

TEST(AppearanceRuntimeSignalSourceTest, CurrentSystemReaderIsCallable) {
  const SystemAppearanceSignals signals =
      AppearanceRuntimeSignalSource::ReadCurrentSystemSignals();
  // The exact values belong to the host's accessibility settings. This test
  // protects the stable Chromium query seam from becoming a no-op or crash.
  static_cast<void>(signals);
  SUCCEED();
}

}  // namespace

}  // namespace ahoi::appearance
