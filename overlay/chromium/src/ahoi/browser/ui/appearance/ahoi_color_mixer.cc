// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/ahoi_color_mixer.h"

#include "ahoi/browser/ui/appearance/opaque_palette.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace ahoi::appearance {

namespace {

using Key = ui::ColorProviderKey;

// Without the spec palette the Ahoi ids follow whatever sys colours the
// user's main colour, theme or contrast setting produced.
void AddDerivedAhoiColors(ui::ColorMixer& mixer) {
  mixer[kColorAhoiFrame] = {ui::kColorFrameActive};
  mixer[kColorAhoiWebSurface] = {ui::kColorSysSurface};
  mixer[kColorAhoiSelection] = {ui::kColorSysTonalContainer};
  mixer[kColorAhoiDisabled] = {ui::kColorSysStateDisabled};
}

void AddSpecPaletteColors(ui::ColorMixer& mixer, bool dark) {
  const OpaquePalette& palette = GetOpaquePalette(dark);

  // Ahoi surfaces.
  mixer[kColorAhoiFrame] = {palette.frame};
  mixer[kColorAhoiWebSurface] = {palette.web_surface};
  mixer[kColorAhoiSelection] = {palette.selection};
  mixer[kColorAhoiDisabled] = {palette.disabled};

  // Accent. Every Material id that references kColorSysPrimary resolves
  // against the final mixer and therefore follows this value.
  mixer[ui::kColorSysPrimary] = {palette.accent};
  mixer[ui::kColorSysOnPrimary] = {palette.on_accent};

  // Ids that the sys mixer derives from kColorRefPrimary* directly. They are
  // re-pointed at the spec so no Google-blue tone mixes with the accent.
  mixer[ui::kColorSysPrimaryContainer] = {kColorAhoiSelection};
  mixer[ui::kColorSysOnPrimaryContainer] = {ui::kColorSysOnSurface};
  mixer[ui::kColorSysStateFocusRing] = {ui::kColorSysPrimary};
  mixer[ui::kColorSysStateRipplePrimary] = ui::SetAlpha(
      {ui::kColorSysPrimary},
      SkColorGetA(ResolvePressedOverlay(palette.accent)));

  // Selection surface. Primary text meets 4.5:1 on it in both appearances.
  mixer[ui::kColorSysTonalContainer] = {kColorAhoiSelection};
  mixer[ui::kColorSysOnTonalContainer] = {ui::kColorSysOnSurface};

  // Text, dividers and states.
  mixer[ui::kColorSysOnSurface] = {palette.primary_text};
  mixer[ui::kColorSysOnSurfaceSubtle] = {palette.secondary_text};
  mixer[ui::kColorSysDivider] = {palette.divider};
  mixer[ui::kColorSysError] = {palette.error};
  mixer[ui::kColorSysStateDisabled] = {kColorAhoiDisabled};
}

}  // namespace

bool ShouldApplyAhoiPalette(const ui::ColorProviderKey& key) {
  if (key.user_color.has_value() ||
      key.user_color_source == Key::UserColorSource::kGrayscale) {
    return false;
  }
  if (key.custom_theme) {
    return false;
  }
  return key.contrast_mode == Key::ContrastMode::kNormal &&
         key.forced_colors == Key::ForcedColors::kNone;
}

void AddAhoiColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  if (!ShouldApplyAhoiPalette(key)) {
    AddDerivedAhoiColors(mixer);
    return;
  }
  AddSpecPaletteColors(mixer, key.color_mode == Key::ColorMode::kDark);
}

}  // namespace ahoi::appearance
