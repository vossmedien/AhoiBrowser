// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/appearance_policy.h"

#include <utility>

namespace ahoi::appearance {

namespace {

struct RoleDefaults {
  ui::ColorId background_color;
  int corner_radius;
  float glass_opacity;
  float glass_blur_sigma;
  int border_thickness;
};

constexpr RoleDefaults GetRoleDefaults(SurfaceRole role) {
  switch (role) {
    case SurfaceRole::kBrowserChrome:
      return {ui::kColorSysSurface2, 0, 0.30f, 28.0f, 0};
    case SurfaceRole::kSidebar:
      return {ui::kColorSysSurface2, 14, 0.46f, 30.0f, 0};
    case SurfaceRole::kFloatingNavigation:
      return {ui::kColorSysSurface3, 14, 0.62f, 30.0f, 0};
    case SurfaceRole::kCommandBar:
      return {ui::kColorSysSurface, 18, 0.68f, 32.0f, 0};
    case SurfaceRole::kPopup:
      return {ui::kColorSysSurface3, 18, 0.70f, 30.0f, 0};
    case SurfaceRole::kDeveloperTools:
      return {ui::kColorSysSurface3, 18, 0.97f, 24.0f, 0};
    case SurfaceRole::kMiniPlayer:
      return {ui::kColorSysSurface4, 14, 0.62f, 26.0f, 0};
    case SurfaceRole::kCount:
      break;
  }
  return {ui::kColorSysSurface, 0, 1.0f, 0.0f, 0};
}

}  // namespace

bool GlassPolicy::AllowsGlass() const {
  return enabled && platform_supports_glass && !system_reduce_transparency &&
         !high_contrast && !battery_saver &&
         performance_pressure == PerformancePressure::kNone;
}

GlassMode AppearanceResolver::ResolveMode(const GlassPolicy& policy) {
  return policy.AllowsGlass() ? GlassMode::kGlass : GlassMode::kOpaque;
}

SurfaceAppearance AppearanceResolver::Resolve(SurfaceRole role,
                                              const GlassPolicy& policy) {
  const RoleDefaults defaults = GetRoleDefaults(role);
  SurfaceAppearance appearance;
  appearance.background_color = defaults.background_color;
  appearance.corner_radius = defaults.corner_radius;
  appearance.border_thickness = defaults.border_thickness;
  appearance.mode = ResolveMode(policy);
  if (appearance.uses_glass()) {
    appearance.opacity = defaults.glass_opacity;
    appearance.background_blur_sigma = defaults.glass_blur_sigma;
  }
  return appearance;
}

AppearanceState::AppearanceState() = default;

AppearanceState::AppearanceState(ChangeCallback changed)
    : changed_(std::move(changed)) {}

AppearanceState::AppearanceState(GlassPolicy policy) : policy_(policy) {}

AppearanceState::AppearanceState(GlassPolicy policy, ChangeCallback changed)
    : policy_(policy), changed_(std::move(changed)) {}

AppearanceState::~AppearanceState() = default;

void AppearanceState::SetPolicy(GlassPolicy policy) {
  if (policy_ == policy) {
    return;
  }
  policy_ = policy;
  if (changed_) {
    changed_.Run(policy_);
  }
}

}  // namespace ahoi::appearance
