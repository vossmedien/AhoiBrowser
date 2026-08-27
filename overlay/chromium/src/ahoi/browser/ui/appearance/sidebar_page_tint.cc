// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_page_tint.h"

#include <algorithm>
#include <cstdint>

#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/color_analysis.h"

namespace ahoi::appearance {
namespace {

// An alpha overlay preserves Chromium's light/dark semantic surface and turns
// even saturated page theme colors into a restrained pastel accent.
constexpr uint8_t kSidebarPageTintAlpha = 0x1c;
constexpr int kMaxFaviconAnalysisDimension = 32;

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
  return color_utils::CalculateKMeanColorOfBitmap(analysis_bitmap);
}

std::optional<SkColor> ResolveSidebarPageTint(
    bool enabled,
    bool high_contrast,
    std::optional<SkColor> page_theme_color,
    std::optional<SkColor> favicon_color) {
  if (!enabled || high_contrast) {
    return std::nullopt;
  }

  const std::optional<SkColor> source_color =
      page_theme_color.has_value() && SkColorGetA(*page_theme_color) != 0
          ? page_theme_color
          : favicon_color;
  if (!source_color.has_value() || SkColorGetA(*source_color) == 0) {
    return std::nullopt;
  }
  return SkColorSetA(*source_color, kSidebarPageTintAlpha);
}

}  // namespace ahoi::appearance
