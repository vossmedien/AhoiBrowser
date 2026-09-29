// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_AHOI_COLOR_MIXER_H_
#define AHOI_BROWSER_UI_APPEARANCE_AHOI_COLOR_MIXER_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace ahoi::appearance {

// Maps Ahoi's opaque spec palette (opaque_palette.h) onto Chromium's semantic
// kColorSys* ids and defines the kColorAhoi* ids from chrome_color_id.h.
//
// The palette only replaces Chromium's defaults. It steps aside, leaving the
// Material values untouched, when the user picked their own main colour
// (including grayscale), when a custom or policy theme is installed, and for
// Increase Contrast or forced colours. The kColorAhoi* ids are then derived
// from the active sys colours, so Ahoi views can always use them.
//
// Must be added after the Material mixers and before the native and custom
// theme mixers: every mixer resolves colour references against the final
// mixer, so Material ids derived from kColorSysPrimary follow the accent.
void AddAhoiColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key);

// True when AddAhoiColorMixer() applies the spec palette for `key`.
bool ShouldApplyAhoiPalette(const ui::ColorProviderKey& key);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_AHOI_COLOR_MIXER_H_
