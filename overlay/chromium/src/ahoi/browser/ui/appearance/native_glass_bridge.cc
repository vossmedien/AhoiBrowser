// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/native_glass_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ahoi::appearance {

NativeChromeMaterialConfiguration ResolveNativeChromeMaterialConfiguration(
    const SurfaceAppearance& appearance,
    SkColor theme_tint_color,
    SkColor resolved_background_color,
    NativeGlassStyle style) {
  const float clamped_opacity = std::clamp(appearance.opacity, 0.0f, 1.0f);
  const auto tint_alpha =
      static_cast<uint8_t>(std::lround(clamped_opacity * 255.0f));
  return {
      .use_native_glass = appearance.uses_glass(),
      .style = style,
      .tint_color = appearance.uses_glass()
                        ? SkColorSetA(theme_tint_color, tint_alpha)
                        : SK_ColorTRANSPARENT,
      .fallback_color = SkColorSetA(resolved_background_color, 0xFF),
      .corner_radius = std::max(0, appearance.corner_radius),
  };
}

}  // namespace ahoi::appearance
