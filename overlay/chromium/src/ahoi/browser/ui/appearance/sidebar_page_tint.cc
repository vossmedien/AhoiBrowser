// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_page_tint.h"

#include <algorithm>
#include <cstdint>

#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/color_utils.h"

namespace ahoi::appearance {
namespace {

// Before a View has a ColorProvider, preserve the historical restrained
// fallback. Once attached, choose the smallest bounded alpha that makes the
// tint visibly distinct from the semantic sidebar surface.
constexpr uint8_t kFallbackSidebarPageTintAlpha = 0x1c;
constexpr uint8_t kMinimumSidebarPageTintAlpha = 0x28;
constexpr uint8_t kMaximumSidebarPageTintAlpha = 0x40;
constexpr uint8_t kSidebarPageTintAlphaStep = 0x04;
constexpr int kMaxFaviconAnalysisDimension = 32;
constexpr int kMinimumBrandColorDistance = 80;
constexpr int kTargetTintDistance = 20;
constexpr uint8_t kMinimumBrandColorAlpha = 0x40;
constexpr double kMinimumBrandColorSaturation = 0.18;
constexpr double kMinimumBrandColorLightness = 0.08;
constexpr double kMaximumBrandColorLightness = 0.92;

const color_utils::HSL kFaviconBrandColorLowerBound = {
    -1.0, kMinimumBrandColorSaturation, kMinimumBrandColorLightness};
const color_utils::HSL kFaviconBrandColorUpperBound = {
    -1.0, 1.0, kMaximumBrandColorLightness};

int SquaredRgbDistance(SkColor lhs, SkColor rhs) {
  const int red = SkColorGetR(lhs) - SkColorGetR(rhs);
  const int green = SkColorGetG(lhs) - SkColorGetG(rhs);
  const int blue = SkColorGetB(lhs) - SkColorGetB(rhs);
  return red * red + green * green + blue * blue;
}

bool IsUsefulBrandColor(
    SkColor color,
    const std::optional<SkColor>& sidebar_background_color) {
  if (SkColorGetA(color) < kMinimumBrandColorAlpha) {
    return false;
  }

  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  if (hsl.s < kMinimumBrandColorSaturation ||
      hsl.l < kMinimumBrandColorLightness ||
      hsl.l > kMaximumBrandColorLightness) {
    return false;
  }

  if (!sidebar_background_color.has_value()) {
    return true;
  }
  constexpr int kMinimumSquaredDistance =
      kMinimumBrandColorDistance * kMinimumBrandColorDistance;
  return SquaredRgbDistance(color, *sidebar_background_color) >=
         kMinimumSquaredDistance;
}

uint8_t ResolveTintAlpha(SkColor source_color, SkColor background_color) {
  source_color = SkColorSetA(source_color, SK_AlphaOPAQUE);
  background_color = SkColorSetA(background_color, SK_AlphaOPAQUE);
  constexpr int kTargetSquaredDistance =
      kTargetTintDistance * kTargetTintDistance;
  for (int alpha = kMinimumSidebarPageTintAlpha;
       alpha <= kMaximumSidebarPageTintAlpha;
       alpha += kSidebarPageTintAlphaStep) {
    const SkColor blended = color_utils::AlphaBlend(
        source_color, background_color, static_cast<SkAlpha>(alpha));
    if (SquaredRgbDistance(blended, background_color) >=
        kTargetSquaredDistance) {
      return static_cast<uint8_t>(alpha);
    }
  }
  return kMaximumSidebarPageTintAlpha;
}

bool HasVisiblePixel(const SkBitmap& bitmap) {
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      if (SkColorGetA(bitmap.getColor(x, y)) != SK_AlphaTRANSPARENT) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::optional<SkColor> ExtractSidebarPageColorFromFavicon(
    const SkBitmap& favicon) {
  if (!favicon.readyToDraw()) {
    return std::nullopt;
  }

  SkBitmap analysis_bitmap = favicon;
  const int max_dimension = std::max(favicon.width(), favicon.height());
  if (max_dimension > kMaxFaviconAnalysisDimension) {
    const int analysis_width = std::max(
        1, static_cast<int>(static_cast<int64_t>(favicon.width()) *
                            kMaxFaviconAnalysisDimension / max_dimension));
    const int analysis_height = std::max(
        1, static_cast<int>(static_cast<int64_t>(favicon.height()) *
                            kMaxFaviconAnalysisDimension / max_dimension));
    analysis_bitmap = skia::ImageOperations::Resize(
        favicon, skia::ImageOperations::RESIZE_GOOD, analysis_width,
        analysis_height);
  }

  if (!analysis_bitmap.readyToDraw() || !HasVisiblePixel(analysis_bitmap)) {
    return std::nullopt;
  }
  return color_utils::CalculateKMeanColorOfBitmap(
      analysis_bitmap, analysis_bitmap.height(),
      kFaviconBrandColorLowerBound, kFaviconBrandColorUpperBound,
      /*find_closest=*/true);
}

std::optional<SkColor> ResolveSidebarPageTint(
    bool enabled,
    bool high_contrast,
    std::optional<SkColor> page_theme_color,
    std::optional<SkColor> favicon_color,
    std::optional<SkColor> sidebar_background_color) {
  if (!enabled || high_contrast) {
    return std::nullopt;
  }

  std::optional<SkColor> source_color;
  if (page_theme_color.has_value() &&
      IsUsefulBrandColor(*page_theme_color, sidebar_background_color)) {
    source_color = page_theme_color;
  } else if (favicon_color.has_value() &&
             IsUsefulBrandColor(*favicon_color, sidebar_background_color)) {
    source_color = favicon_color;
  }
  if (!source_color.has_value()) {
    return std::nullopt;
  }
  const uint8_t alpha = sidebar_background_color.has_value()
                            ? ResolveTintAlpha(*source_color,
                                               *sidebar_background_color)
                            : kFallbackSidebarPageTintAlpha;
  return SkColorSetA(*source_color, alpha);
}

}  // namespace ahoi::appearance
