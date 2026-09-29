// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/opaque_palette.h"

#include <algorithm>
#include <cmath>

namespace ahoi::appearance {

namespace {

constexpr OpaquePalette kLightPalette = {
    .frame = SkColorSetRGB(0xE8, 0xEF, 0xF0),
    .web_surface = SkColorSetRGB(0xFA, 0xFB, 0xFA),
    .primary_text = SkColorSetRGB(0x14, 0x2D, 0x35),
    .secondary_text = SkColorSetRGB(0x4E, 0x62, 0x6A),
    .accent = SkColorSetRGB(0x00, 0x6B, 0x73),
    .on_accent = SkColorSetRGB(0xFF, 0xFF, 0xFF),
    .selection = SkColorSetRGB(0xCD, 0xE7, 0xE8),
    .divider = SkColorSetRGB(0xBB, 0xCD, 0xD0),
    .error = SkColorSetRGB(0xB4, 0x23, 0x32),
    .disabled = SkColorSetRGB(0x71, 0x81, 0x87),
};

constexpr OpaquePalette kDarkPalette = {
    .frame = SkColorSetRGB(0x17, 0x26, 0x2C),
    .web_surface = SkColorSetRGB(0x11, 0x1A, 0x1F),
    .primary_text = SkColorSetRGB(0xED, 0xF4, 0xF5),
    .secondary_text = SkColorSetRGB(0xAD, 0xBF, 0xC5),
    .accent = SkColorSetRGB(0x70, 0xD6, 0xDE),
    .on_accent = SkColorSetRGB(0x14, 0x2D, 0x35),
    .selection = SkColorSetRGB(0x24, 0x4A, 0x53),
    .divider = SkColorSetRGB(0x40, 0x56, 0x5E),
    .error = SkColorSetRGB(0xFF, 0x9A, 0xA4),
    .disabled = SkColorSetRGB(0x75, 0x87, 0x8E),
};

SkAlpha ToAlpha(float opacity) {
  return static_cast<SkAlpha>(
      std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

const OpaquePalette& GetOpaquePalette(bool dark) {
  return dark ? kDarkPalette : kLightPalette;
}

SkColor ResolveHoverOverlay(SkColor primary_text, bool dark) {
  return SkColorSetA(primary_text,
                     ToAlpha(dark ? palette_tokens::kHoverOverlayDark
                                  : palette_tokens::kHoverOverlayLight));
}

SkColor ResolvePressedOverlay(SkColor accent) {
  return SkColorSetA(accent, ToAlpha(palette_tokens::kPressedAccentOverlay));
}

}  // namespace ahoi::appearance
