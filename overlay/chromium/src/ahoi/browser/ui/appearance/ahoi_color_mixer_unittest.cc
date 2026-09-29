// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/ahoi_color_mixer.h"

#include "ahoi/browser/ui/appearance/opaque_palette.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace ahoi::appearance {

namespace {

using Key = ui::ColorProviderKey;

// Stand-ins for the values Chromium's sys and Material mixers produce.
constexpr SkColor kMaterialPrimary = SkColorSetRGB(0x0B, 0x57, 0xD0);
constexpr SkColor kMaterialFocusRing = SkColorSetRGB(0x0A, 0x56, 0xCF);
constexpr SkColor kMaterialTonal = SkColorSetRGB(0xD3, 0xE3, 0xFD);
constexpr SkColor kMaterialOnSurface = SkColorSetRGB(0x1F, 0x1F, 0x1F);
constexpr SkColor kMaterialFrame = SkColorSetRGB(0xDA, 0xDC, 0xE0);

class FakeThemeSupplier : public Key::ThemeInitializerSupplier {
 public:
  FakeThemeSupplier() : ThemeInitializerSupplier(ThemeType::kExtension) {}

  void AddColorMixers(ui::ColorProvider*, const Key&) const override {}
  bool GetColor(int, SkColor*) const override { return false; }
  bool GetTint(int, color_utils::HSL*) const override { return false; }
  bool GetDisplayProperty(int, int*) const override { return false; }
  bool HasCustomImage(int) const override { return false; }

 private:
  ~FakeThemeSupplier() override = default;
};

class AhoiColorMixerTest : public testing::Test {
 protected:
  // Mirrors AddChromeColorMixers(): a Material mixer that derives a control
  // colour from a sys id runs before the Ahoi mixer.
  void Build(const Key& key) {
    ui::ColorMixer& material = provider_.AddMixer();
    material[ui::kColorSysPrimary] = {kMaterialPrimary};
    material[ui::kColorSysStateFocusRing] = {kMaterialFocusRing};
    material[ui::kColorSysTonalContainer] = {kMaterialTonal};
    material[ui::kColorSysOnSurface] = {kMaterialOnSurface};
    material[ui::kColorFrameActive] = {kMaterialFrame};
    material[ui::kColorFocusableBorderFocused] = {ui::kColorSysStateFocusRing};
    material[ui::kColorButtonBackgroundProminent] = {ui::kColorSysPrimary};
    AddAhoiColorMixer(&provider_, key);
  }

  SkColor Get(ui::ColorId id) const { return provider_.GetColor(id); }

  void ExpectSpecPalette(bool dark) {
    const OpaquePalette& palette = GetOpaquePalette(dark);
    EXPECT_EQ(palette.accent, Get(ui::kColorSysPrimary));
    EXPECT_EQ(palette.on_accent, Get(ui::kColorSysOnPrimary));
    EXPECT_EQ(palette.accent, Get(ui::kColorSysStateFocusRing));
    EXPECT_EQ(palette.selection, Get(ui::kColorSysTonalContainer));
    EXPECT_EQ(palette.primary_text, Get(ui::kColorSysOnTonalContainer));
    EXPECT_EQ(palette.selection, Get(ui::kColorSysPrimaryContainer));
    EXPECT_EQ(palette.divider, Get(ui::kColorSysDivider));
    EXPECT_EQ(palette.error, Get(ui::kColorSysError));
    EXPECT_EQ(palette.primary_text, Get(ui::kColorSysOnSurface));
    EXPECT_EQ(palette.secondary_text, Get(ui::kColorSysOnSurfaceSubtle));
    EXPECT_EQ(palette.disabled, Get(ui::kColorSysStateDisabled));
    EXPECT_EQ(ResolvePressedOverlay(palette.accent),
              Get(ui::kColorSysStateRipplePrimary));

    EXPECT_EQ(palette.frame, Get(kColorAhoiFrame));
    EXPECT_EQ(palette.web_surface, Get(kColorAhoiWebSurface));
    EXPECT_EQ(palette.selection, Get(kColorAhoiSelection));
    EXPECT_EQ(palette.disabled, Get(kColorAhoiDisabled));

    // Ids derived by earlier mixers follow the accent consistently.
    EXPECT_EQ(palette.accent, Get(ui::kColorFocusableBorderFocused));
    EXPECT_EQ(palette.accent, Get(ui::kColorButtonBackgroundProminent));
  }

  void ExpectMaterialUntouched() {
    EXPECT_EQ(kMaterialPrimary, Get(ui::kColorSysPrimary));
    EXPECT_EQ(kMaterialFocusRing, Get(ui::kColorSysStateFocusRing));
    EXPECT_EQ(kMaterialFocusRing, Get(ui::kColorFocusableBorderFocused));
    EXPECT_EQ(kMaterialTonal, Get(ui::kColorSysTonalContainer));
    EXPECT_EQ(kMaterialOnSurface, Get(ui::kColorSysOnSurface));

    // The Ahoi ids still resolve, from the active sys colours.
    EXPECT_EQ(kMaterialFrame, Get(kColorAhoiFrame));
    EXPECT_EQ(kMaterialTonal, Get(kColorAhoiSelection));
  }

  ui::ColorProvider provider_;
};

TEST_F(AhoiColorMixerTest, LightMapsTheSpecPalette) {
  Key key;
  key.color_mode = Key::ColorMode::kLight;
  key.user_color_source = Key::UserColorSource::kBaseline;
  ASSERT_TRUE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectSpecPalette(/*dark=*/false);
}

TEST_F(AhoiColorMixerTest, DarkMapsTheSpecPalette) {
  Key key;
  key.color_mode = Key::ColorMode::kDark;
  key.user_color_source = Key::UserColorSource::kBaseline;
  ASSERT_TRUE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectSpecPalette(/*dark=*/true);
}

TEST_F(AhoiColorMixerTest, UserMainColourIsNotOverridden) {
  Key key;
  key.user_color = SkColorSetRGB(0xB0, 0x30, 0x60);
  key.user_color_source = Key::UserColorSource::kAccent;
  EXPECT_FALSE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectMaterialUntouched();
}

TEST_F(AhoiColorMixerTest, GrayscaleIsNotOverridden) {
  Key key;
  key.user_color_source = Key::UserColorSource::kGrayscale;
  EXPECT_FALSE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectMaterialUntouched();
}

TEST_F(AhoiColorMixerTest, HighContrastIsNotOverridden) {
  Key key;
  key.color_mode = Key::ColorMode::kDark;
  key.contrast_mode = Key::ContrastMode::kHigh;
  EXPECT_FALSE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectMaterialUntouched();
}

TEST_F(AhoiColorMixerTest, CustomThemeIsNotOverridden) {
  Key key;
  key.custom_theme = base::MakeRefCounted<FakeThemeSupplier>();
  EXPECT_FALSE(ShouldApplyAhoiPalette(key));
  Build(key);
  ExpectMaterialUntouched();
}

}  // namespace

}  // namespace ahoi::appearance
