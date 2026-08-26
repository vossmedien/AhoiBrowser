// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_policy.h"

#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::appearance {

namespace {

TEST(AppearancePolicyTest, GlassIsEnabledForTheDefaultMacPolicy) {
  const GlassPolicy policy;
  EXPECT_TRUE(policy.AllowsGlass());
  EXPECT_EQ(GlassMode::kGlass, AppearanceResolver::ResolveMode(policy));

  const SurfaceAppearance sidebar =
      AppearanceResolver::Resolve(SurfaceRole::kSidebar, policy);
  EXPECT_TRUE(sidebar.uses_glass());
  EXPECT_LT(sidebar.opacity, 1.0f);
  EXPECT_GT(sidebar.background_blur_sigma, 0.0f);
  EXPECT_EQ(ui::kColorSysSurface2, sidebar.background_color);
  EXPECT_EQ(0, sidebar.border_thickness);
}

TEST(AppearancePolicyTest, EverySafetyGateUsesOpaqueDeterministicFallback) {
  const GlassPolicy policies[] = {
      {.enabled = false},
      {.platform_supports_glass = false},
      {.system_reduce_transparency = true},
      {.high_contrast = true},
      {.battery_saver = true},
      {.performance_pressure = PerformancePressure::kElevated},
      {.performance_pressure = PerformancePressure::kCritical},
  };

  for (const GlassPolicy& policy : policies) {
    EXPECT_FALSE(policy.AllowsGlass());
    EXPECT_EQ(GlassMode::kOpaque, AppearanceResolver::ResolveMode(policy));
    for (SurfaceRole role :
         {SurfaceRole::kBrowserChrome, SurfaceRole::kSidebar,
          SurfaceRole::kFloatingNavigation, SurfaceRole::kCommandBar,
          SurfaceRole::kPopup, SurfaceRole::kDeveloperTools,
          SurfaceRole::kMiniPlayer}) {
      const SurfaceAppearance appearance =
          AppearanceResolver::Resolve(role, policy);
      EXPECT_FALSE(appearance.uses_glass());
      EXPECT_FLOAT_EQ(1.0f, appearance.opacity);
      EXPECT_FLOAT_EQ(0.0f, appearance.background_blur_sigma);
    }
  }
}

TEST(AppearancePolicyTest, ReducedMotionDoesNotInventAnotherPalette) {
  GlassPolicy policy;
  policy.reduced_motion = true;

  EXPECT_TRUE(policy.AllowsGlass());
  EXPECT_EQ(GlassMode::kGlass, AppearanceResolver::ResolveMode(policy));
}

TEST(AppearancePolicyTest, SurfaceRolesRemainSemanticAndDistinct) {
  const GlassPolicy policy;
  EXPECT_NE(AppearanceResolver::Resolve(SurfaceRole::kSidebar, policy),
            AppearanceResolver::Resolve(SurfaceRole::kCommandBar, policy));
  EXPECT_EQ(ui::kColorSysSurface3,
            AppearanceResolver::Resolve(SurfaceRole::kPopup, policy)
                .background_color);
  const SurfaceAppearance developer =
      AppearanceResolver::Resolve(SurfaceRole::kDeveloperTools, policy);
  EXPECT_GT(developer.opacity,
            AppearanceResolver::Resolve(SurfaceRole::kPopup, policy).opacity);
  EXPECT_LT(developer.opacity, 1.0f);
  EXPECT_EQ(ui::kColorSysSurface4,
            AppearanceResolver::Resolve(SurfaceRole::kMiniPlayer, policy)
                .background_color);
}

TEST(AppearancePolicyTest, ProductGlassSurfacesUseDepthInsteadOfOutlines) {
  const GlassPolicy policy;
  for (SurfaceRole role :
       {SurfaceRole::kBrowserChrome, SurfaceRole::kSidebar,
        SurfaceRole::kFloatingNavigation, SurfaceRole::kCommandBar,
        SurfaceRole::kPopup, SurfaceRole::kDeveloperTools,
        SurfaceRole::kMiniPlayer}) {
    EXPECT_EQ(0, AppearanceResolver::Resolve(role, policy).border_thickness);
  }
}

TEST(AppearancePolicyTest, StateNotifiesOnlyWhenPolicyChanges) {
  std::vector<GlassPolicy> changes;
  AppearanceState state(base::BindRepeating(
      [](std::vector<GlassPolicy>* changes, const GlassPolicy& policy) {
        changes->push_back(policy);
      },
      &changes));

  GlassPolicy policy;
  policy.battery_saver = true;
  state.SetPolicy(policy);
  state.SetPolicy(policy);

  ASSERT_EQ(1u, changes.size());
  EXPECT_TRUE(changes.front().battery_saver);
  EXPECT_EQ(GlassMode::kOpaque, state.mode());
}

TEST(AppearancePolicyTest, PolicyCanRecoverFromPerformancePressure) {
  GlassPolicy policy;
  policy.performance_pressure = PerformancePressure::kCritical;
  AppearanceState state(policy);
  EXPECT_EQ(GlassMode::kOpaque, state.mode());

  policy.performance_pressure = PerformancePressure::kNone;
  state.SetPolicy(policy);
  EXPECT_EQ(GlassMode::kGlass, state.mode());
}

}  // namespace

}  // namespace ahoi::appearance
