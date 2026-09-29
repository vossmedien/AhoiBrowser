// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/glass_material.h"

#include <cstdlib>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace ahoi::appearance {

namespace {

constexpr SurfaceRole kAllRoles[] = {
    SurfaceRole::kBrowserChrome,
    SurfaceRole::kSidebar,
    SurfaceRole::kFloatingNavigation,
    SurfaceRole::kCommandBar,
    SurfaceRole::kPopup,
    SurfaceRole::kDeveloperTools,
    SurfaceRole::kMiniPlayer,
};

constexpr SurfaceHost kAllHosts[] = {
    SurfaceHost::kWindowBackdrop,
    SurfaceHost::kOverNativeBackdrop,
    SurfaceHost::kOverWebContent,
    SurfaceHost::kOwnWindow,
};

// Representative resolved ColorProvider surfaces.
constexpr SkColor kLightSurface = SkColorSetRGB(0xEE, 0xF0, 0xF4);
constexpr SkColor kDarkSurface = SkColorSetRGB(0x1F, 0x20, 0x24);

float Alpha(SkColor color) {
  return SkColorGetA(color) / 255.0f;
}

unsigned ShadowAlpha(Elevation elevation, bool dark) {
  return SkColorGetA(ResolveElevationShadow(elevation, dark).color);
}

TEST(GlassMaterialTest, RolesLiveOnTheirContractHosts) {
  EXPECT_EQ(SurfaceHost::kWindowBackdrop,
            DefaultHostForRole(SurfaceRole::kBrowserChrome));
  EXPECT_EQ(SurfaceHost::kOverNativeBackdrop,
            DefaultHostForRole(SurfaceRole::kSidebar, false));
  EXPECT_EQ(SurfaceHost::kOverWebContent,
            DefaultHostForRole(SurfaceRole::kSidebar, true));
  EXPECT_EQ(SurfaceHost::kOverNativeBackdrop,
            DefaultHostForRole(SurfaceRole::kMiniPlayer, false));
  EXPECT_EQ(SurfaceHost::kOwnWindow,
            DefaultHostForRole(SurfaceRole::kCommandBar));
  EXPECT_EQ(SurfaceHost::kOwnWindow,
            DefaultHostForRole(SurfaceRole::kDeveloperTools));
  EXPECT_EQ(SurfaceHost::kOverWebContent,
            DefaultHostForRole(SurfaceRole::kFloatingNavigation));
  EXPECT_EQ(SurfaceHost::kOverWebContent,
            DefaultHostForRole(SurfaceRole::kPopup));
}

TEST(GlassMaterialTest, GlassOnUsesNativeGlassWhereAppKitCanRenderIt) {
  const GlassPolicy policy;
  EXPECT_EQ(MaterialBacking::kNativeGlass,
            ResolveMaterialBacking(SurfaceHost::kWindowBackdrop, policy, true));
  EXPECT_EQ(MaterialBacking::kNativeGlass,
            ResolveMaterialBacking(SurfaceHost::kOwnWindow, policy, true));
  EXPECT_EQ(
      MaterialBacking::kVeil,
      ResolveMaterialBacking(SurfaceHost::kOverNativeBackdrop, policy, true));
  EXPECT_EQ(MaterialBacking::kCompositorGlass,
            ResolveMaterialBacking(SurfaceHost::kOverWebContent, policy, true));
}

TEST(GlassMaterialTest, MissingNativeGlassNeverLeavesASeeThroughSheet) {
  const GlassPolicy policy;
  EXPECT_EQ(MaterialBacking::kOpaque,
            ResolveMaterialBacking(SurfaceHost::kOwnWindow, policy, false));
  EXPECT_EQ(
      MaterialBacking::kOpaque,
      ResolveMaterialBacking(SurfaceHost::kOverNativeBackdrop, policy, false));
  EXPECT_FALSE(ResolveNativeBackdrop(SurfaceHost::kWindowBackdrop, policy,
                                     false, kDarkSurface)
                   .use_native_glass);
  const SurfaceAppearance panel = ResolveHostedSurfaceAppearance(
      SurfaceRole::kCommandBar, SurfaceHost::kOwnWindow, policy, false);
  EXPECT_FALSE(panel.uses_glass());
  EXPECT_FLOAT_EQ(1.0f, panel.opacity);
}

TEST(GlassMaterialTest, EverySafetyGateResolvesEveryHostToOpaque) {
  const GlassPolicy policies[] = {
      {.enabled = false},
      {.platform_supports_glass = false},
      {.system_reduce_transparency = true},
      {.high_contrast = true},
      {.battery_saver = true},
      {.performance_pressure = PerformancePressure::kElevated},
  };
  for (const GlassPolicy& policy : policies) {
    for (SurfaceHost host : kAllHosts) {
      EXPECT_EQ(MaterialBacking::kOpaque,
                ResolveMaterialBacking(host, policy, true));
      const NativeBackdropSpec spec =
          ResolveNativeBackdrop(host, policy, true, kLightSurface);
      EXPECT_FALSE(spec.use_native_glass);
      EXPECT_EQ(SK_ColorTRANSPARENT, spec.glass_tint);
      EXPECT_EQ(SK_ColorTRANSPARENT, spec.window_foundation);
      EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(spec.opaque_fill));
      for (SurfaceRole role : kAllRoles) {
        const SurfaceAppearance appearance =
            ResolveHostedSurfaceAppearance(role, host, policy, true);
        // Glass OFF / fallback is exactly the established opaque look.
        EXPECT_EQ(AppearanceResolver::Resolve(role, policy), appearance);
        EXPECT_FALSE(appearance.uses_glass());
        EXPECT_FLOAT_EQ(1.0f, appearance.opacity);
        EXPECT_FLOAT_EQ(0.0f, appearance.background_blur_sigma);
      }
    }
  }
}

TEST(GlassMaterialTest, ReducedMotionAloneKeepsTheMaterial) {
  GlassPolicy policy;
  policy.reduced_motion = true;
  EXPECT_EQ(MaterialBacking::kNativeGlass,
            ResolveMaterialBacking(SurfaceHost::kWindowBackdrop, policy, true));
}

TEST(GlassMaterialTest, OnlyOneLayerEverBlursAPixel) {
  const GlassPolicy policy;
  for (SurfaceRole role : kAllRoles) {
    for (SurfaceHost host : kAllHosts) {
      const MaterialBacking backing =
          ResolveMaterialBacking(host, policy, true);
      const SurfaceAppearance appearance =
          ResolveHostedSurfaceAppearance(role, host, policy, true);
      if (backing == MaterialBacking::kNativeGlass ||
          backing == MaterialBacking::kVeil) {
        EXPECT_FLOAT_EQ(0.0f, appearance.background_blur_sigma);
        EXPECT_LT(appearance.opacity, 1.0f);
      }
    }
  }
}

TEST(GlassMaterialTest, DockedSidebarIsAThinVeilOverTheWindowGlass) {
  const GlassPolicy policy;
  const SurfaceAppearance docked = ResolveHostedSurfaceAppearance(
      SurfaceRole::kSidebar, DefaultHostForRole(SurfaceRole::kSidebar, false),
      policy, true);
  EXPECT_TRUE(docked.uses_glass());
  EXPECT_FLOAT_EQ(glass_tokens::kSidebarVeil, docked.opacity);
  EXPECT_LT(docked.opacity, 0.5f);
  EXPECT_FLOAT_EQ(0.0f, docked.background_blur_sigma);

  const SurfaceAppearance floating = ResolveHostedSurfaceAppearance(
      SurfaceRole::kSidebar, DefaultHostForRole(SurfaceRole::kSidebar, true),
      policy, true);
  EXPECT_GT(floating.background_blur_sigma, 0.0f);
  EXPECT_GE(floating.opacity, glass_tokens::kOverWebMinimumTint);
}

TEST(GlassMaterialTest, SurfacesOverPagesKeepTheirContrastFloor) {
  const GlassPolicy policy;
  for (SurfaceRole role : kAllRoles) {
    const SurfaceAppearance appearance = ResolveHostedSurfaceAppearance(
        role, SurfaceHost::kOverWebContent, policy, true);
    EXPECT_GE(appearance.opacity, glass_tokens::kOverWebMinimumTint);
  }
}

TEST(GlassMaterialTest, DeveloperToolsStayDenserThanTheCommandBar) {
  const GlassPolicy policy;
  const SurfaceAppearance tools = ResolveHostedSurfaceAppearance(
      SurfaceRole::kDeveloperTools, SurfaceHost::kOwnWindow, policy, true);
  const SurfaceAppearance command = ResolveHostedSurfaceAppearance(
      SurfaceRole::kCommandBar, SurfaceHost::kOwnWindow, policy, true);
  EXPECT_GT(tools.opacity, command.opacity);
  EXPECT_EQ(glass_tokens::kPanelRadius, command.corner_radius);
}

TEST(GlassMaterialTest, BackdropIsMilkyInLightAndDarkAppearance) {
  const GlassPolicy policy;
  for (SkColor surface : {kLightSurface, kDarkSurface}) {
    const NativeBackdropSpec spec = ResolveNativeBackdrop(
        SurfaceHost::kWindowBackdrop, policy, true, surface);
    ASSERT_TRUE(spec.use_native_glass);
    // A visible milk tint, but never so dense that the glass turns flat.
    EXPECT_GT(Alpha(spec.glass_tint), 0.25f);
    EXPECT_LT(Alpha(spec.glass_tint), 0.6f);
    // The foundation below the glass is weaker than the tint itself.
    EXPECT_GT(Alpha(spec.window_foundation), 0.0f);
    EXPECT_LT(Alpha(spec.window_foundation), Alpha(spec.glass_tint));
    EXPECT_EQ(SkColorSetA(surface, SK_AlphaOPAQUE), spec.opaque_fill);
    // The tint is lighter than the theme surface: that is the "milk".
    EXPECT_GT(color_utils::GetRelativeLuminance(
                  SkColorSetA(spec.glass_tint, SK_AlphaOPAQUE)),
              color_utils::GetRelativeLuminance(surface));
  }
}

TEST(GlassMaterialTest, MilkKeepsTheAppearanceItWasDerivedFrom) {
  EXPECT_TRUE(color_utils::IsDark(ResolveMilkyBase(kDarkSurface)));
  EXPECT_FALSE(color_utils::IsDark(ResolveMilkyBase(kLightSurface)));
  EXPECT_EQ(SK_AlphaOPAQUE,
            SkColorGetA(ResolveMilkyBase(SkColorSetA(kDarkSurface, 0x40))));
}

TEST(GlassMaterialTest, WorkspaceAccentOnlyNudgesTheMilk) {
  const SkColor accent = SkColorSetRGB(0xE4, 0x5E, 0x68);
  const SkColor plain = ResolveMilkyBase(kLightSurface);
  const SkColor tinted = ResolveMilkyBase(kLightSurface, accent);
  EXPECT_NE(plain, tinted);
  EXPECT_GT(SkColorGetR(tinted) - SkColorGetG(tinted),
            SkColorGetR(plain) - SkColorGetG(plain));
  // At most a light nudge: every channel stays within the accent share.
  const int max_delta = static_cast<int>(glass_tokens::kAccentShare * 255) + 1;
  EXPECT_LE(std::abs(SkColorGetG(tinted) - SkColorGetG(plain)), max_delta);
  EXPECT_LE(std::abs(SkColorGetB(tinted) - SkColorGetB(plain)), max_delta);
}

TEST(GlassMaterialTest, PanelsTintTheGlassWithoutAWindowFoundation) {
  const NativeBackdropSpec spec = ResolveNativeBackdrop(
      SurfaceHost::kOwnWindow, GlassPolicy(), true, kDarkSurface);
  ASSERT_TRUE(spec.use_native_glass);
  EXPECT_EQ(SK_ColorTRANSPARENT, spec.window_foundation);
  EXPECT_GT(Alpha(spec.glass_tint), 0.0f);
  // In-window hosts never own an NSGlassEffectView.
  EXPECT_FALSE(ResolveNativeBackdrop(SurfaceHost::kOverWebContent,
                                     GlassPolicy(), true, kDarkSurface)
                   .use_native_glass);
}

TEST(GlassMaterialTest, IncreaseContrastOutlinesOnlyFloatingSurfaces) {
  GlassPolicy policy;
  policy.high_contrast = true;
  for (SurfaceRole role : kAllRoles) {
    const SurfaceAppearance appearance = ResolveHostedSurfaceAppearance(
        role, DefaultHostForRole(role), policy, true);
    EXPECT_FALSE(appearance.uses_glass());
    const bool embedded =
        role == SurfaceRole::kBrowserChrome || role == SurfaceRole::kSidebar;
    EXPECT_EQ(embedded ? 0 : 1, appearance.border_thickness);
  }
  // Without Increase Contrast glass surfaces separate by depth, not lines.
  for (SurfaceRole role : kAllRoles) {
    EXPECT_EQ(0, ResolveHostedSurfaceAppearance(role, DefaultHostForRole(role),
                                                GlassPolicy(), true)
                     .border_thickness);
  }
}

// DESIGN_SPEC 2026-09-29, geometry and material tables.
TEST(GlassMaterialTest, GeometryTokensMatchTheDesignSpec) {
  EXPECT_EQ(14, glass_tokens::kEmbeddedRadius);
  EXPECT_EQ(18, glass_tokens::kPanelRadius);
  EXPECT_EQ(8, glass_tokens::kRowRadius);
  EXPECT_EQ(6, glass_tokens::kRowInset);
  EXPECT_EQ(22, glass_tokens::kWindowRadius);
}

TEST(GlassMaterialTest, HairlineIsTranslucentWhite) {
  const SkColor light = ResolveGlassHairline(false);
  const SkColor dark = ResolveGlassHairline(true);
  EXPECT_EQ(SK_ColorWHITE, SkColorSetA(light, SK_AlphaOPAQUE));
  EXPECT_EQ(SK_ColorWHITE, SkColorSetA(dark, SK_AlphaOPAQUE));
  EXPECT_EQ(115u, SkColorGetA(light));  // 45 %
  EXPECT_EQ(36u, SkColorGetA(dark));    // 14 %
}

TEST(GlassMaterialTest, ElevationShadowsFollowTheSpec) {
  const ElevationShadow window =
      ResolveElevationShadow(Elevation::kWindow, false);
  EXPECT_EQ(16, window.y_offset);
  EXPECT_EQ(48, window.blur);
  EXPECT_EQ(SkColorSetARGB(41, 10, 28, 34), window.color);
  EXPECT_EQ(82u, ShadowAlpha(Elevation::kWindow, true));

  const ElevationShadow panel =
      ResolveElevationShadow(Elevation::kPanel, true);
  EXPECT_EQ(12, panel.y_offset);
  EXPECT_EQ(32, panel.blur);
  EXPECT_EQ(SkColorSetARGB(102, 8, 25, 30), panel.color);
  EXPECT_EQ(46u, ShadowAlpha(Elevation::kPanel, false));

  const ElevationShadow card =
      ResolveElevationShadow(Elevation::kContentCard, false);
  EXPECT_EQ(2, card.y_offset);
  EXPECT_EQ(8, card.blur);
  EXPECT_EQ(15u, SkColorGetA(card.color));
  EXPECT_EQ(46u, ShadowAlpha(Elevation::kContentCard, true));
  // Dark appearance always needs the deeper shadow.
  for (Elevation elevation :
       {Elevation::kWindow, Elevation::kPanel, Elevation::kContentCard}) {
    EXPECT_GT(ShadowAlpha(elevation, true), ShadowAlpha(elevation, false));
  }
}

TEST(GlassMaterialTest, MaterialAlphasMatchTheDesignSpec) {
  EXPECT_FLOAT_EQ(0.42f, glass_tokens::kBackdropTintLight);
  EXPECT_FLOAT_EQ(0.40f, glass_tokens::kBackdropTintDark);
  EXPECT_FLOAT_EQ(0.30f, glass_tokens::kWindowFoundation);
  EXPECT_FLOAT_EQ(0.22f, glass_tokens::kSidebarVeil);
  EXPECT_FLOAT_EQ(0.40f, glass_tokens::kMiniPlayerVeil);
  EXPECT_FLOAT_EQ(0.30f, glass_tokens::kPanelTintLight);
  EXPECT_FLOAT_EQ(0.34f, glass_tokens::kPanelTintDark);
  EXPECT_FLOAT_EQ(0.32f, glass_tokens::kCommandBarVeil);
  EXPECT_FLOAT_EQ(0.62f, glass_tokens::kOverWebMinimumTint);
  EXPECT_FLOAT_EQ(0.55f, glass_tokens::kMilkLiftLight);
  EXPECT_FLOAT_EQ(0.12f, glass_tokens::kMilkLiftDark);
  EXPECT_FLOAT_EQ(0.08f, glass_tokens::kAccentShare);
}

}  // namespace

}  // namespace ahoi::appearance
