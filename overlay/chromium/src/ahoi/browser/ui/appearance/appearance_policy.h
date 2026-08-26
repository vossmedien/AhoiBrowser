// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_POLICY_H_
#define AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_POLICY_H_

#include <cstdint>

#include "base/functional/callback.h"
#include "ui/color/color_id.h"

namespace ahoi::appearance {

// The resolver intentionally keeps platform policy separate from the visual
// roles. On macOS the caller can feed this from NSWorkspace/accessibility and
// power notifications; other platforms can simply leave platform support
// disabled and receive the same deterministic opaque result.
enum class PerformancePressure : uint8_t {
  kNone,
  kElevated,
  kCritical,
};

struct GlassPolicy {
  bool enabled = true;
  bool platform_supports_glass = true;
  bool system_reduce_transparency = false;
  bool high_contrast = false;
  bool reduced_motion = false;
  bool battery_saver = false;
  PerformancePressure performance_pressure = PerformancePressure::kNone;

  bool AllowsGlass() const;
};

constexpr bool operator==(const GlassPolicy& lhs, const GlassPolicy& rhs) {
  return lhs.enabled == rhs.enabled &&
         lhs.platform_supports_glass == rhs.platform_supports_glass &&
         lhs.system_reduce_transparency == rhs.system_reduce_transparency &&
         lhs.high_contrast == rhs.high_contrast &&
         lhs.reduced_motion == rhs.reduced_motion &&
         lhs.battery_saver == rhs.battery_saver &&
         lhs.performance_pressure == rhs.performance_pressure;
}

constexpr bool operator!=(const GlassPolicy& lhs, const GlassPolicy& rhs) {
  return !(lhs == rhs);
}

enum class GlassMode : uint8_t {
  kOpaque,
  kGlass,
};

// These are material roles, rather than component-specific colors. The
// semantic ColorIds are resolved by the active Chromium ColorProvider, so
// light/dark/high-contrast themes remain authoritative at paint time.
enum class SurfaceRole : uint8_t {
  kBrowserChrome,
  kSidebar,
  kFloatingNavigation,
  kCommandBar,
  kPopup,
  // Developer surfaces favor readability of dense form and cookie data. They
  // retain a subtle material effect while being substantially less translucent
  // than ordinary transient popups.
  kDeveloperTools,
  kMiniPlayer,
  kCount,
};

struct SurfaceAppearance {
  ui::ColorId background_color = ui::kColorSysSurface;
  ui::ColorId foreground_color = ui::kColorSysOnSurface;
  ui::ColorId border_color = ui::kColorSysOutline;
  ui::ColorId hover_color = ui::kColorSysStateHoverOnSubtle;
  ui::ColorId scrim_color = ui::kColorSysStateScrim;
  float opacity = 1.0f;
  float background_blur_sigma = 0.0f;
  int corner_radius = 0;
  int border_thickness = 1;
  GlassMode mode = GlassMode::kOpaque;

  bool uses_glass() const { return mode == GlassMode::kGlass; }
};

constexpr bool operator==(const SurfaceAppearance& lhs,
                          const SurfaceAppearance& rhs) {
  return lhs.background_color == rhs.background_color &&
         lhs.foreground_color == rhs.foreground_color &&
         lhs.border_color == rhs.border_color &&
         lhs.hover_color == rhs.hover_color &&
         lhs.scrim_color == rhs.scrim_color && lhs.opacity == rhs.opacity &&
         lhs.background_blur_sigma == rhs.background_blur_sigma &&
         lhs.corner_radius == rhs.corner_radius &&
         lhs.border_thickness == rhs.border_thickness && lhs.mode == rhs.mode;
}

constexpr bool operator!=(const SurfaceAppearance& lhs,
                          const SurfaceAppearance& rhs) {
  return !(lhs == rhs);
}

// Pure resolver used by both native Views and any future Swift/HTML shell
// bridge. It never returns transparent paint values when policy disallows
// glass, which keeps accessibility and low-power fallback deterministic.
class AppearanceResolver final {
 public:
  static GlassMode ResolveMode(const GlassPolicy& policy);
  static SurfaceAppearance Resolve(SurfaceRole role, const GlassPolicy& policy);
};

// Small mutable policy holder for a browser window. It is intentionally
// independent of PrefService and macOS notifications; those integrations can
// update one instance without making the appearance package own lifecycle or
// platform observers.
class AppearanceState final {
 public:
  using ChangeCallback = base::RepeatingCallback<void(const GlassPolicy&)>;

  AppearanceState();
  explicit AppearanceState(ChangeCallback changed);
  explicit AppearanceState(GlassPolicy policy);
  AppearanceState(GlassPolicy policy, ChangeCallback changed);
  AppearanceState(const AppearanceState&) = delete;
  AppearanceState& operator=(const AppearanceState&) = delete;
  ~AppearanceState();

  void SetPolicy(GlassPolicy policy);

  const GlassPolicy& policy() const { return policy_; }
  GlassMode mode() const { return AppearanceResolver::ResolveMode(policy_); }
  SurfaceAppearance Resolve(SurfaceRole role) const {
    return AppearanceResolver::Resolve(role, policy_);
  }

 private:
  GlassPolicy policy_;
  ChangeCallback changed_;
};

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_APPEARANCE_POLICY_H_
