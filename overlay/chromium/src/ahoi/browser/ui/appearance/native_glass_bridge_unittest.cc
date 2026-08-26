// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/native_glass_bridge.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::appearance {

TEST(NativeGlassBridgeTest, GlassConfigurationUsesThemeTintAndRoleGeometry) {
  SurfaceAppearance appearance;
  appearance.mode = GlassMode::kGlass;
  appearance.opacity = 0.3f;
  appearance.corner_radius = 18;

  const NativeChromeMaterialConfiguration configuration =
      ResolveNativeChromeMaterialConfiguration(
          appearance, SkColorSetRGB(0x22, 0x44, 0x66),
          SkColorSetRGB(0x10, 0x20, 0x30), NativeGlassStyle::kClear);

  EXPECT_TRUE(configuration.use_native_glass);
  EXPECT_EQ(NativeGlassStyle::kClear, configuration.style);
  EXPECT_EQ(18, configuration.corner_radius);
  EXPECT_EQ(77u, SkColorGetA(configuration.tint_color));
  EXPECT_EQ(SkColorSetRGB(0x10, 0x20, 0x30), configuration.fallback_color);
}

TEST(NativeGlassBridgeTest, OpaquePolicyNeverLeavesTranslucentFallback) {
  SurfaceAppearance appearance;
  appearance.mode = GlassMode::kOpaque;
  appearance.opacity = 0.2f;
  appearance.corner_radius = -4;

  const NativeChromeMaterialConfiguration configuration =
      ResolveNativeChromeMaterialConfiguration(
          appearance, SkColorSetARGB(0x55, 0x22, 0x44, 0x66),
          SkColorSetARGB(0x20, 0x10, 0x20, 0x30));

  EXPECT_FALSE(configuration.use_native_glass);
  EXPECT_EQ(SK_ColorTRANSPARENT, configuration.tint_color);
  EXPECT_EQ(0xFFu, SkColorGetA(configuration.fallback_color));
  EXPECT_EQ(0, configuration.corner_radius);
}

TEST(NativeGlassBridgeTest, TintOpacityIsClampedAtNativeBoundary) {
  SurfaceAppearance appearance;
  appearance.mode = GlassMode::kGlass;

  appearance.opacity = 2.0f;
  EXPECT_EQ(0xFFu, SkColorGetA(ResolveNativeChromeMaterialConfiguration(
                                   appearance, SK_ColorWHITE, SK_ColorBLACK)
                                   .tint_color));

  appearance.opacity = -1.0f;
  EXPECT_EQ(0u, SkColorGetA(ResolveNativeChromeMaterialConfiguration(
                                appearance, SK_ColorWHITE, SK_ColorBLACK)
                                .tint_color));
}

}  // namespace ahoi::appearance
