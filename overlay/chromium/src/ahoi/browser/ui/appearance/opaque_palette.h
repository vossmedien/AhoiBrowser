// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_OPAQUE_PALETTE_H_
#define AHOI_BROWSER_UI_APPEARANCE_OPAQUE_PALETTE_H_

#include "third_party/skia/include/core/SkColor.h"

namespace ahoi::appearance {

// Ahoi's semantic palette (design spec 2026-09-29, "Typografie, Palette und
// Zustände"). These are the opaque reference values: the frame colour is
// also the Reduce Transparency / Glass OFF fallback, and every text colour
// meets 4.5:1 on the frame, web and selection surfaces of its appearance.
//
// Views must not paint these values directly. They are the source values
// for an Ahoi ColorMixer, which maps them onto ColorIds so light, dark,
// Increase Contrast and the user's global main colour stay authoritative
// inside Chromium's ColorProvider.
struct OpaquePalette {
  // Opaque frame, sidebar and window fallback.
  SkColor frame;
  // Opaque web card, dialog and form field surface.
  SkColor web_surface;
  SkColor primary_text;
  SkColor secondary_text;
  // Default accent and keyboard focus ring.
  SkColor accent;
  // Text and icons on an accent-filled primary button.
  SkColor on_accent;
  SkColor selection;
  SkColor divider;
  SkColor error;
  // Disabled text and icons; never reacts to hover.
  SkColor disabled;
};

const OpaquePalette& GetOpaquePalette(bool dark);

namespace palette_tokens {

// Additional Workspace dot colours. A dot is never the only state cue.
inline constexpr SkColor kWorkspaceAmber = SkColorSetRGB(0xB8, 0x6C, 0x16);
inline constexpr SkColor kWorkspaceViolet = SkColorSetRGB(0x79, 0x61, 0xB3);

// Interaction states. Hover overlays the primary text colour, pressed and
// active rows overlay the accent; neither shifts layout.
inline constexpr float kHoverOverlayLight = 0.06f;
inline constexpr float kHoverOverlayDark = 0.08f;
inline constexpr float kPressedAccentOverlay = 0.12f;

// Keyboard focus: an accent outline with a gap to the focused control. It is
// never replaced by a glass highlight.
inline constexpr int kFocusRingThickness = 2;
inline constexpr int kFocusRingGap = 2;
// Error state: a one-point field outline plus a written message.
inline constexpr int kErrorOutlineThickness = 1;

}  // namespace palette_tokens

// Translucent state overlays for a surface in `dark` or light appearance.
// `primary_text` and `accent` are the resolved ColorProvider colours.
SkColor ResolveHoverOverlay(SkColor primary_text, bool dark);
SkColor ResolvePressedOverlay(SkColor accent);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_OPAQUE_PALETTE_H_
