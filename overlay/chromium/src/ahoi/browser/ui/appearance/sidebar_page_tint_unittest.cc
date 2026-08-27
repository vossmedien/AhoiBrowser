// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_page_tint.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace ahoi::appearance {
namespace {

TEST(SidebarPageTintTest, UsesSubtleOpaquePageColorWhenEnabled) {
  const std::optional<SkColor> tint =
      ResolveSidebarPageTint(true, false, SkColorSetRGB(0x22, 0x88, 0xee));

  ASSERT_TRUE(tint.has_value());
  EXPECT_EQ(0x1cu, SkColorGetA(*tint));
  EXPECT_EQ(0x22u, SkColorGetR(*tint));
  EXPECT_EQ(0x88u, SkColorGetG(*tint));
  EXPECT_EQ(0xeeu, SkColorGetB(*tint));
}

TEST(SidebarPageTintTest, RespectsToggleContrastAndMissingThemeColor) {
  const SkColor page_color = SkColorSetRGB(0x22, 0x88, 0xee);
  EXPECT_FALSE(ResolveSidebarPageTint(false, false, page_color).has_value());
  EXPECT_FALSE(ResolveSidebarPageTint(true, true, page_color).has_value());
  EXPECT_FALSE(ResolveSidebarPageTint(true, false, std::nullopt).has_value());
  EXPECT_FALSE(
      ResolveSidebarPageTint(true, false, SK_ColorTRANSPARENT).has_value());
}

TEST(SidebarPageTintTest, FallsBackToFaviconAndPrefersThemeColor) {
  const SkColor theme_color = SkColorSetRGB(0x12, 0x34, 0x56);
  const SkColor favicon_color = SkColorSetRGB(0xaa, 0xbb, 0xcc);

  const std::optional<SkColor> fallback =
      ResolveSidebarPageTint(true, false, std::nullopt, favicon_color);
  ASSERT_TRUE(fallback.has_value());
  EXPECT_EQ(SkColorSetARGB(0x1c, 0xaa, 0xbb, 0xcc), *fallback);

  const std::optional<SkColor> transparent_theme_fallback =
      ResolveSidebarPageTint(true, false, SK_ColorTRANSPARENT, favicon_color);
  ASSERT_TRUE(transparent_theme_fallback.has_value());
  EXPECT_EQ(*fallback, *transparent_theme_fallback);

  const std::optional<SkColor> preferred_theme =
      ResolveSidebarPageTint(true, false, theme_color, favicon_color);
  ASSERT_TRUE(preferred_theme.has_value());
  EXPECT_EQ(SkColorSetARGB(0x1c, 0x12, 0x34, 0x56), *preferred_theme);

  EXPECT_FALSE(ResolveSidebarPageTint(false, false, std::nullopt, favicon_color)
                   .has_value());
  EXPECT_FALSE(ResolveSidebarPageTint(true, true, std::nullopt, favicon_color)
                   .has_value());
}

TEST(SidebarPageTintTest, ExtractsLoadedFaviconColorWithBoundedAnalysis) {
  SkBitmap favicon;
  favicon.allocN32Pixels(128, 64);
  favicon.eraseColor(SkColorSetRGB(0x55, 0x99, 0xdd));

  const std::optional<SkColor> color =
      ExtractSidebarPageColorFromFavicon(favicon);
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(SkColorSetRGB(0x55, 0x99, 0xdd), *color);
}

TEST(SidebarPageTintTest, RejectsMissingAndFullyTransparentFavicons) {
  EXPECT_FALSE(ExtractSidebarPageColorFromFavicon(SkBitmap()).has_value());

  SkBitmap favicon;
  favicon.allocN32Pixels(16, 16);
  favicon.eraseColor(SK_ColorTRANSPARENT);
  EXPECT_FALSE(ExtractSidebarPageColorFromFavicon(favicon).has_value());
}

}  // namespace
}  // namespace ahoi::appearance
