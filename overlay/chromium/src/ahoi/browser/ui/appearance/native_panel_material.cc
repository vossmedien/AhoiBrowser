// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/native_panel_material.h"

#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/appearance/glass_material.h"
#include "base/check.h"
#include "build/build_config.h"
#include "ui/color/color_provider.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/ui/appearance/native_glass_bridge.h"
#endif

namespace ahoi::appearance {

#if !BUILDFLAG(IS_MAC)
// Non-Mac builds never create a bridge; a complete type is still needed for
// the unique_ptr member.
class NativeChromeMaterialBridge {};
#endif

NativePanelMaterial::NativePanelMaterial(SurfaceRole role) : role_(role) {}

NativePanelMaterial::~NativePanelMaterial() {
  Detach();
}

void NativePanelMaterial::Apply(views::View* contents,
                                const GlassPolicy& policy) {
  CHECK(contents);
  policy_ = policy;
  views::Widget* const widget = contents->GetWidget();
  views::View* const client_view = widget ? widget->client_view() : nullptr;
  if (!client_view) {
    // Not yet shown. Paint a deterministic opaque panel; the native glass is
    // attached once the bubble widget exists.
    Detach();
    ApplySurfaceAppearance(
        contents,
        ResolveHostedSurfaceAppearance(role_, SurfaceHost::kOwnWindow, policy_,
                                       /*native_glass_available=*/
                                       false));
    return;
  }

  if (panel_ != client_view) {
    Detach();
    panel_ = client_view;
    observation_.Observe(client_view);
  }
  surface_ = ResolveHostedSurfaceAppearance(
      role_, SurfaceHost::kOwnWindow, policy_, IsNativeBackdropAvailable());
  ClearSurfaceBackgroundAppearance(contents);
  ApplySurfaceBackgroundAppearance(client_view, surface_);
  SyncNativeMaterial();
}

bool NativePanelMaterial::is_native_for_testing() const {
#if BUILDFLAG(IS_MAC)
  return bridge_ && bridge_->is_using_native_glass_for_testing();
#else
  return false;
#endif
}

void NativePanelMaterial::OnViewBoundsChanged(views::View* observed_view) {
  SyncNativeMaterial();
}

void NativePanelMaterial::OnViewThemeChanged(views::View* observed_view) {
  // Light/dark and user-colour changes re-resolve the milky tint.
  SyncNativeMaterial();
}

void NativePanelMaterial::OnViewRemovedFromWidget(views::View* observed_view) {
  Detach();
}

void NativePanelMaterial::OnViewIsDeleting(views::View* observed_view) {
  Detach();
}

void NativePanelMaterial::Detach() {
  observation_.Reset();
  panel_ = nullptr;
#if BUILDFLAG(IS_MAC)
  if (bridge_) {
    bridge_->Reset();
  }
#endif
  bridge_.reset();
}

void NativePanelMaterial::SyncNativeMaterial() {
#if BUILDFLAG(IS_MAC)
  if (!panel_ || !panel_->GetWidget() || !panel_->GetColorProvider()) {
    return;
  }
  const NativeBackdropSpec spec = ResolveNativeBackdrop(
      SurfaceHost::kOwnWindow, policy_, IsNativeBackdropAvailable(),
      panel_->GetColorProvider()->GetColor(surface_.background_color));
  if (!spec.use_native_glass) {
    if (bridge_) {
      bridge_->Reset();
    }
    return;
  }
  if (!bridge_) {
    bridge_ = std::make_unique<NativeChromeMaterialBridge>(
        panel_->GetWidget()->GetNativeWindow());
  }
  bridge_->ApplyToRegion(spec,
                         panel_->ConvertRectToWidget(panel_->GetLocalBounds()),
                         surface_.corner_radius);
#endif
}

}  // namespace ahoi::appearance
