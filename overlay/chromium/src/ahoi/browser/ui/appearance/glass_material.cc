// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/glass_material.h"

#include <algorithm>
#include <cmath>

#include "build/build_config.h"
#include "ui/gfx/color_utils.h"

#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/ui/appearance/native_glass_bridge.h"
#endif

namespace ahoi::appearance {

namespace {

SkAlpha ToAlpha(float opacity) {
  return static_cast<SkAlpha>(
      std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
}

// Views paint on top of native glass. The browser backdrop itself has no
// Views surface of its own, so its veil is fully clear.
float NativeVeilForRole(SurfaceRole role) {
  switch (role) {
    case SurfaceRole::kBrowserChrome:
      return 0.0f;
    case SurfaceRole::kCommandBar:
      return glass_tokens::kCommandBarVeil;
    case SurfaceRole::kDeveloperTools:
      return glass_tokens::kDeveloperToolsVeil;
    case SurfaceRole::kSidebar:
      return glass_tokens::kSidebarVeil;
    case SurfaceRole::kMiniPlayer:
      return glass_tokens::kMiniPlayerVeil;
    case SurfaceRole::kFloatingNavigation:
    case SurfaceRole::kPopup:
    case SurfaceRole::kCount:
      break;
  }
  return glass_tokens::kCommandBarVeil;
}

SurfaceAppearance MakeOpaque(SurfaceAppearance appearance) {
  appearance.mode = GlassMode::kOpaque;
  appearance.opacity = 1.0f;
  appearance.background_blur_sigma = 0.0f;
  return appearance;
}

}  // namespace

bool IsNativeBackdropAvailable() {
#if BUILDFLAG(IS_MAC)
  return IsNativeMacGlassAvailable();
#else
  return false;
#endif
}

SurfaceHost DefaultHostForRole(SurfaceRole role, bool sidebar_floating) {
  switch (role) {
    case SurfaceRole::kBrowserChrome:
      return SurfaceHost::kWindowBackdrop;
    case SurfaceRole::kSidebar:
      return sidebar_floating ? SurfaceHost::kOverWebContent
                              : SurfaceHost::kOverNativeBackdrop;
    case SurfaceRole::kMiniPlayer:
      // The MiniPlayer lives inside the Sidebar and follows its host.
      return sidebar_floating ? SurfaceHost::kOverWebContent
                              : SurfaceHost::kOverNativeBackdrop;
    case SurfaceRole::kCommandBar:
    case SurfaceRole::kDeveloperTools:
      return SurfaceHost::kOwnWindow;
    case SurfaceRole::kFloatingNavigation:
    case SurfaceRole::kPopup:
    case SurfaceRole::kCount:
      break;
  }
  return SurfaceHost::kOverWebContent;
}

MaterialBacking ResolveMaterialBacking(SurfaceHost host,
                                       const GlassPolicy& policy,
                                       bool native_glass_available) {
  if (!policy.AllowsGlass()) {
    return MaterialBacking::kOpaque;
  }
  switch (host) {
    case SurfaceHost::kWindowBackdrop:
    case SurfaceHost::kOwnWindow:
      // A compositor blur inside a separate window has nothing to sample, so
      // without native glass these would be a flat see-through sheet.
      return native_glass_available ? MaterialBacking::kNativeGlass
                                    : MaterialBacking::kOpaque;
    case SurfaceHost::kOverNativeBackdrop:
      return native_glass_available ? MaterialBacking::kVeil
                                    : MaterialBacking::kOpaque;
    case SurfaceHost::kOverWebContent:
      return MaterialBacking::kCompositorGlass;
  }
  return MaterialBacking::kOpaque;
}

SurfaceAppearance ResolveHostedSurfaceAppearance(SurfaceRole role,
                                                 SurfaceHost host,
                                                 const GlassPolicy& policy,
                                                 bool native_glass_available) {
  SurfaceAppearance appearance = AppearanceResolver::Resolve(role, policy);
  switch (ResolveMaterialBacking(host, policy, native_glass_available)) {
    case MaterialBacking::kOpaque:
      return MakeOpaque(appearance);
    case MaterialBacking::kNativeGlass:
    case MaterialBacking::kVeil:
      // Exactly one layer blurs: the native glass below this surface.
      appearance.opacity = NativeVeilForRole(role);
      appearance.background_blur_sigma = 0.0f;
      return appearance;
    case MaterialBacking::kCompositorGlass:
      appearance.opacity =
          std::max(appearance.opacity, glass_tokens::kOverWebMinimumTint);
      return appearance;
  }
  return MakeOpaque(appearance);
}

SkColor ResolveMilkyBase(SkColor surface_color, std::optional<SkColor> accent) {
  const SkColor opaque_surface = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  const float lift = color_utils::IsDark(opaque_surface)
                         ? glass_tokens::kMilkLiftDark
                         : glass_tokens::kMilkLiftLight;
  SkColor base = color_utils::AlphaBlend(SK_ColorWHITE, opaque_surface, lift);
  if (accent.has_value()) {
    base = color_utils::AlphaBlend(SkColorSetA(*accent, SK_AlphaOPAQUE), base,
                                   glass_tokens::kAccentShare);
  }
  return SkColorSetA(base, SK_AlphaOPAQUE);
}

NativeBackdropSpec ResolveNativeBackdrop(SurfaceHost host,
                                         const GlassPolicy& policy,
                                         bool native_glass_available,
                                         SkColor surface_color,
                                         std::optional<SkColor> accent) {
  NativeBackdropSpec spec;
  spec.opaque_fill = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  const bool native_host =
      host == SurfaceHost::kWindowBackdrop || host == SurfaceHost::kOwnWindow;
  if (!native_host ||
      ResolveMaterialBacking(host, policy, native_glass_available) !=
          MaterialBacking::kNativeGlass) {
    return spec;
  }
  const bool dark = color_utils::IsDark(spec.opaque_fill);
  const SkColor base = ResolveMilkyBase(surface_color, accent);
  float tint = 0.0f;
  if (host == SurfaceHost::kWindowBackdrop) {
    tint = dark ? glass_tokens::kBackdropTintDark
                : glass_tokens::kBackdropTintLight;
    spec.window_foundation =
        SkColorSetA(base, ToAlpha(glass_tokens::kWindowFoundation));
  } else {
    tint = dark ? glass_tokens::kPanelTintDark : glass_tokens::kPanelTintLight;
  }
  spec.use_native_glass = true;
  spec.glass_tint = SkColorSetA(base, ToAlpha(tint));
  return spec;
}

SkColor ResolveGlassHairline(bool dark) {
  return SkColorSetA(SK_ColorWHITE,
                     ToAlpha(dark ? glass_tokens::kHairlineDark
                                  : glass_tokens::kHairlineLight));
}

ElevationShadow ResolveElevationShadow(Elevation elevation, bool dark) {
  namespace t = glass_tokens;
  switch (elevation) {
    case Elevation::kWindow:
      return {t::kWindowShadowY, t::kWindowShadowBlur,
              SkColorSetA(t::kWindowShadowColor,
                          ToAlpha(dark ? t::kWindowShadowDark
                                       : t::kWindowShadowLight))};
    case Elevation::kPanel:
      return {t::kPanelShadowY, t::kPanelShadowBlur,
              SkColorSetA(t::kSurfaceShadowColor,
                          ToAlpha(dark ? t::kPanelShadowDark
                                       : t::kPanelShadowLight))};
    case Elevation::kContentCard:
      return {t::kContentCardShadowY, t::kContentCardShadowBlur,
              SkColorSetA(t::kSurfaceShadowColor,
                          ToAlpha(dark ? t::kContentCardShadowDark
                                       : t::kContentCardShadowLight))};
  }
  return {};
}

}  // namespace ahoi::appearance
