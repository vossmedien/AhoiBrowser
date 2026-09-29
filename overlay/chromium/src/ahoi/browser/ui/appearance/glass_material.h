// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_GLASS_MATERIAL_H_
#define AHOI_BROWSER_UI_APPEARANCE_GLASS_MATERIAL_H_

#include <cstdint>
#include <optional>

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "third_party/skia/include/core/SkColor.h"

namespace ahoi::appearance {

// Ahoi's Liquid Glass design system. Every surface sits on exactly one of
// these hosts; the host decides who is allowed to blur. Only one layer ever
// blurs a given pixel, which keeps the material clear instead of muddy.
//
//  kWindowBackdrop    The browser window itself. One NSGlassEffectView covers
//                     the whole window below Chromium's Views; WebContents is
//                     an opaque sibling above it and is never blurred.
//  kOverNativeBackdrop In-window chrome resting on the window backdrop (the
//                     docked Sidebar and its MiniPlayer). Paints a thin milky
//                     veil only. The backdrop already blurs, so no second
//                     compositor blur is allowed.
//  kOverWebContent    In-window chrome floating over page pixels (floating
//                     navigation row, reveal notch, popup overlay chrome,
//                     Link-Peek, floating Sidebar). AppKit cannot interleave a
//                     native view between compositor layers, so these use the
//                     compositor backdrop blur with a denser tint for text
//                     contrast against arbitrary pages.
//  kOwnWindow         Separate bubble windows (command bar, developer tool
//                     surfaces). A native NSGlassEffectView sits behind the
//                     visible panel inside the bubble window; Views paints a
//                     light veil and no compositor blur.
enum class SurfaceHost : uint8_t {
  kWindowBackdrop,
  kOverNativeBackdrop,
  kOverWebContent,
  kOwnWindow,
};

// Who renders the material for a resolved surface.
enum class MaterialBacking : uint8_t {
  // Accessibility, power, user preference or platform: fully opaque paint.
  kOpaque,
  // NSGlassEffectView renders blur/refraction; Views paints tint only.
  kNativeGlass,
  // Views/cc backdrop blur (in-window over web content).
  kCompositorGlass,
  // Views tint only, resting on an ancestor's native glass.
  kVeil,
};

// True when this platform can render NSGlassEffectView (macOS 26+). The
// product GlassPolicy still decides whether it is used.
bool IsNativeBackdropAvailable();

// The default host of each semantic role. The Sidebar is the only role whose
// host depends on presentation: docked it rests on the window backdrop,
// floating it hovers over the content card.
SurfaceHost DefaultHostForRole(SurfaceRole role, bool sidebar_floating = false);

// Chooses the backing for a surface. Native glass must actually be available
// for kWindowBackdrop/kOwnWindow; kOverNativeBackdrop degrades to a compositor
// blur when the backdrop is missing so the surface never becomes see-through.
MaterialBacking ResolveMaterialBacking(SurfaceHost host,
                                       const GlassPolicy& policy,
                                       bool native_glass_available);

// Resolves the full paint contract for a role on a concrete host. Opaque
// results are identical to AppearanceResolver::Resolve() for the role, so
// Glass OFF keeps the established opaque look. Increase Contrast adds a
// one-pixel semantic outline to floating surfaces so their edge stays
// discernible without translucency.
SurfaceAppearance ResolveHostedSurfaceAppearance(SurfaceRole role,
                                                 SurfaceHost host,
                                                 const GlassPolicy& policy,
                                                 bool native_glass_available);

// Window-level material for the browser backdrop and for bubble panels.
struct NativeBackdropSpec {
  bool use_native_glass = false;
  // Colour handed to NSGlassEffectView.tintColor (alpha included). AppKit
  // tints the glass itself, so there is no separate tint view whose z-order
  // relative to the glass would be undefined.
  SkColor glass_tint = SK_ColorTRANSPARENT;
  // Translucent NSWindow background below the glass. It only matters where
  // AppKit momentarily draws no glass (resize, space switch) and prevents a
  // sharp desktop from showing through.
  SkColor window_foundation = SK_ColorTRANSPARENT;
  // Fully opaque colour used whenever native glass is not in use.
  SkColor opaque_fill = SK_ColorBLACK;
};

constexpr bool operator==(const NativeBackdropSpec& lhs,
                          const NativeBackdropSpec& rhs) {
  return lhs.use_native_glass == rhs.use_native_glass &&
         lhs.glass_tint == rhs.glass_tint &&
         lhs.window_foundation == rhs.window_foundation &&
         lhs.opaque_fill == rhs.opaque_fill;
}

// `surface_color` is the resolved ColorProvider colour of the role's
// background ColorId (light/dark/high-contrast/user colour already applied).
// `accent` is an optional Workspace accent which is folded in very lightly.
NativeBackdropSpec ResolveNativeBackdrop(SurfaceHost host,
                                         const GlassPolicy& policy,
                                         bool native_glass_available,
                                         SkColor surface_color,
                                         std::optional<SkColor> accent = {});

// The milky base colour: the theme surface lifted toward white so glass reads
// frosted in light and dark appearance, optionally nudged to an accent.
SkColor ResolveMilkyBase(SkColor surface_color,
                         std::optional<SkColor> accent = {});

// Design tokens shared by every glass surface. Keep new surfaces on these
// values rather than introducing per-surface numbers.
namespace glass_tokens {

// Radii: 14 DIP for window-embedded surfaces (content card, docked/floating
// Sidebar, navigation row, MiniPlayer); 18 DIP for free-floating panels
// (command bar, popups, developer tools); inner rows 8 DIP (concentric with a
// 6 DIP inset inside a 14 DIP container).
inline constexpr int kEmbeddedRadius = 14;
inline constexpr int kPanelRadius = 18;
inline constexpr int kRowRadius = 8;
inline constexpr int kRowInset = 6;
static_assert(kRowRadius + kRowInset == kEmbeddedRadius,
              "inner rows must stay concentric with embedded surfaces");
// Outer corner of the browser window itself.
inline constexpr int kWindowRadius = 22;

// Hairlines are white at these alphas and exactly one device pixel wide.
// They mark glass edges; opaque surfaces use the semantic divider instead.
inline constexpr float kHairlineLight = 0.45f;
inline constexpr float kHairlineDark = 0.14f;

// Milky lift toward white of the theme surface.
inline constexpr float kMilkLiftLight = 0.55f;
inline constexpr float kMilkLiftDark = 0.12f;
// Maximum share of a Workspace accent in the milky base.
inline constexpr float kAccentShare = 0.08f;

// Alpha of the native glass tint. Light glass is a white-milk veil, dark glass
// a lifted graphite; both stay below the point where the glass turns flat.
inline constexpr float kBackdropTintLight = 0.42f;
inline constexpr float kBackdropTintDark = 0.40f;
inline constexpr float kPanelTintLight = 0.30f;
inline constexpr float kPanelTintDark = 0.34f;
inline constexpr float kWindowFoundation = 0.30f;

// Views veil alphas on top of native glass (kVeil / kNativeGlass).
inline constexpr float kSidebarVeil = 0.22f;
inline constexpr float kMiniPlayerVeil = 0.40f;
inline constexpr float kCommandBarVeil = 0.32f;
// Dense tables and forms keep a near-opaque veil for readability.
inline constexpr float kDeveloperToolsVeil = 0.86f;

// Compositor glass over arbitrary page pixels needs a denser tint than glass
// over the calm window backdrop. No over-web surface may go below this floor.
inline constexpr float kOverWebMinimumTint = 0.62f;

// Elevation shadows (CSS-style y offset and blur radius in DIP). The shadow
// colour is a deep sea-green rather than black so it stays calm on glass.
inline constexpr int kWindowShadowY = 16;
inline constexpr int kWindowShadowBlur = 48;
inline constexpr SkColor kWindowShadowColor = SkColorSetRGB(10, 28, 34);
inline constexpr float kWindowShadowLight = 0.16f;
inline constexpr float kWindowShadowDark = 0.32f;
inline constexpr int kPanelShadowY = 12;
inline constexpr int kPanelShadowBlur = 32;
inline constexpr SkColor kSurfaceShadowColor = SkColorSetRGB(8, 25, 30);
inline constexpr float kPanelShadowLight = 0.18f;
inline constexpr float kPanelShadowDark = 0.40f;
inline constexpr int kContentCardShadowY = 2;
inline constexpr int kContentCardShadowBlur = 8;
inline constexpr float kContentCardShadowLight = 0.06f;
inline constexpr float kContentCardShadowDark = 0.18f;

}  // namespace glass_tokens

// The one-device-pixel white hairline along glass edges.
SkColor ResolveGlassHairline(bool dark);

enum class Elevation : uint8_t {
  // The browser window on the desktop.
  kWindow,
  // Free-floating panels: command bar, Peek frame, dialogs.
  kPanel,
  // The opaque web content card inside the window.
  kContentCard,
};

// A shadow token; `blur` is a CSS blur radius (sigma = blur / 2), which is
// also what gfx::ShadowValue expects.
struct ElevationShadow {
  int y_offset = 0;
  int blur = 0;
  SkColor color = SK_ColorTRANSPARENT;
};

constexpr bool operator==(const ElevationShadow& lhs,
                          const ElevationShadow& rhs) {
  return lhs.y_offset == rhs.y_offset && lhs.blur == rhs.blur &&
         lhs.color == rhs.color;
}

ElevationShadow ResolveElevationShadow(Elevation elevation, bool dark);

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_GLASS_MATERIAL_H_
