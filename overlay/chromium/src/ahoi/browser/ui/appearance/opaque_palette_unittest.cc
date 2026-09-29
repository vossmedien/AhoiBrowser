// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/opaque_palette.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace ahoi::appearance {

namespace {

constexpr float kTextContrast = 4.5f;
constexpr float kControlContrast = 3.0f;

TEST(OpaquePaletteTest, ValuesMatchTheDesignSpec) {
  const OpaquePalette& light = GetOpaquePalette(false);
  EXPECT_EQ(SkColorSetRGB(0xE8, 0xEF, 0xF0), light.frame);
  EXPECT_EQ(SkColorSetRGB(0xFA, 0xFB, 0xFA), light.web_surface);
  EXPECT_EQ(SkColorSetRGB(0x14, 0x2D, 0x35), light.primary_text);
  EXPECT_EQ(SkColorSetRGB(0x00, 0x6B, 0x73), light.accent);
  EXPECT_EQ(SK_ColorWHITE, light.on_accent);
  EXPECT_EQ(SkColorSetRGB(0xB4, 0x23, 0x32), light.error);

  const OpaquePalette& dark = GetOpaquePalette(true);
  EXPECT_EQ(SkColorSetRGB(0x17, 0x26, 0x2C), dark.frame);
  EXPECT_EQ(SkColorSetRGB(0x11, 0x1A, 0x1F), dark.web_surface);
  EXPECT_EQ(SkColorSetRGB(0xED, 0xF4, 0xF5), dark.primary_text);
  EXPECT_EQ(SkColorSetRGB(0x70, 0xD6, 0xDE), dark.accent);
  EXPECT_EQ(dark.on_accent, light.primary_text);
  EXPECT_EQ(SkColorSetRGB(0xFF, 0x9A, 0xA4), dark.error);
}

TEST(OpaquePaletteTest, EveryColourIsOpaque) {
  for (bool is_dark : {false, true}) {
    const OpaquePalette& p = GetOpaquePalette(is_dark);
    for (SkColor color :
         {p.frame, p.web_surface, p.primary_text, p.secondary_text, p.accent,
          p.on_accent, p.selection, p.divider, p.error, p.disabled}) {
      EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(color));
    }
  }
}

TEST(OpaquePaletteTest, TextMeetsTheContrastTargetsOnEverySurface) {
  for (bool is_dark : {false, true}) {
    SCOPED_TRACE(is_dark ? "dark" : "light");
    const OpaquePalette& p = GetOpaquePalette(is_dark);
    EXPECT_EQ(is_dark, color_utils::IsDark(p.frame));
    for (SkColor surface : {p.frame, p.web_surface, p.selection}) {
      EXPECT_GE(color_utils::GetContrastRatio(p.primary_text, surface),
                kTextContrast);
      EXPECT_GE(color_utils::GetContrastRatio(p.secondary_text, surface),
                kTextContrast);
      EXPECT_GE(color_utils::GetContrastRatio(p.error, surface),
                kTextContrast);
      // The focus ring and accent controls need 3:1.
      EXPECT_GE(color_utils::GetContrastRatio(p.accent, surface),
                kControlContrast);
    }
    EXPECT_GE(color_utils::GetContrastRatio(p.on_accent, p.accent),
              kTextContrast);
    // Disabled stays distinguishable from its surface, yet quieter than
    // secondary text.
    EXPECT_GE(color_utils::GetContrastRatio(p.disabled, p.web_surface),
              kControlContrast);
    EXPECT_LT(color_utils::GetContrastRatio(p.disabled, p.web_surface),
              color_utils::GetContrastRatio(p.secondary_text, p.web_surface));
  }
}

TEST(OpaquePaletteTest, StateOverlaysKeepTheirHue) {
  const SkColor text = GetOpaquePalette(false).primary_text;
  EXPECT_EQ(15u, SkColorGetA(ResolveHoverOverlay(text, false)));
  EXPECT_EQ(20u, SkColorGetA(ResolveHoverOverlay(text, true)));
  EXPECT_EQ(text, SkColorSetA(ResolveHoverOverlay(text, true),
                              SK_AlphaOPAQUE));
  const SkColor accent = GetOpaquePalette(true).accent;
  EXPECT_EQ(31u, SkColorGetA(ResolvePressedOverlay(accent)));
  EXPECT_EQ(accent,
            SkColorSetA(ResolvePressedOverlay(accent), SK_AlphaOPAQUE));
}

TEST(OpaquePaletteTest, StateTokensMatchTheDesignSpec) {
  EXPECT_EQ(2, palette_tokens::kFocusRingThickness);
  EXPECT_EQ(2, palette_tokens::kFocusRingGap);
  EXPECT_EQ(1, palette_tokens::kErrorOutlineThickness);
}

}  // namespace

}  // namespace ahoi::appearance
