// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_NATIVE_GLASS_BRIDGE_H_
#define AHOI_BROWSER_UI_APPEARANCE_NATIVE_GLASS_BRIDGE_H_

#include <cstdint>
#include <memory>

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/native_ui_types.h"

namespace ahoi::appearance {

// AppKit offers two native glass recipes. Keeping this semantic choice outside
// Objective-C++ makes it possible for product surfaces to select a recipe
// without importing AppKit into Views code.
enum class NativeGlassStyle : uint8_t {
  kRegular,
  kClear,
};

struct NativeChromeMaterialConfiguration {
  bool use_native_glass = false;
  NativeGlassStyle style = NativeGlassStyle::kRegular;
  SkColor tint_color = SK_ColorTRANSPARENT;
  SkColor fallback_color = SK_ColorBLACK;
  int corner_radius = 0;
};

constexpr bool operator==(const NativeChromeMaterialConfiguration& lhs,
                          const NativeChromeMaterialConfiguration& rhs) {
  return lhs.use_native_glass == rhs.use_native_glass &&
         lhs.style == rhs.style && lhs.tint_color == rhs.tint_color &&
         lhs.fallback_color == rhs.fallback_color &&
         lhs.corner_radius == rhs.corner_radius;
}

constexpr bool operator!=(const NativeChromeMaterialConfiguration& lhs,
                          const NativeChromeMaterialConfiguration& rhs) {
  return !(lhs == rhs);
}

// Converts the platform-neutral appearance result and theme colors into the
// complete native material contract. `resolved_background_color` is used for
// the deterministic opaque fallback; `theme_tint_color` keeps native glass in
// sync with Chromium's current light/dark/user-color theme.
NativeChromeMaterialConfiguration ResolveNativeChromeMaterialConfiguration(
    const SurfaceAppearance& appearance,
    SkColor theme_tint_color,
    SkColor resolved_background_color,
    NativeGlassStyle style = NativeGlassStyle::kRegular);

// Owns the AppKit material below the browser's Chromium content hierarchy.
// WebContents remains an opaque sibling above this background, so the native
// effect is visible only through transparent browser-chrome regions and never
// blurs or intercepts input intended for page content.
class NativeChromeMaterialBridge final {
 public:
  explicit NativeChromeMaterialBridge(gfx::NativeWindow window);
  NativeChromeMaterialBridge(const NativeChromeMaterialBridge&) = delete;
  NativeChromeMaterialBridge& operator=(const NativeChromeMaterialBridge&) =
      delete;
  ~NativeChromeMaterialBridge();

  void Apply(const NativeChromeMaterialConfiguration& configuration);
  void Reset();

  bool is_using_native_glass_for_testing() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Runtime availability includes the OS check as well as class discovery. It is
// deliberately narrower than the product policy: Reduce Transparency, High
// Contrast and performance gates remain owned by GlassPolicy.
bool IsNativeMacGlassAvailable();

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_NATIVE_GLASS_BRIDGE_H_
