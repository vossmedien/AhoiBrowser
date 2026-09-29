// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_APPEARANCE_NATIVE_PANEL_MATERIAL_H_
#define AHOI_BROWSER_UI_APPEARANCE_NATIVE_PANEL_MATERIAL_H_

#include <memory>

#include "ahoi/browser/ui/appearance/appearance_policy.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace ahoi::appearance {

class NativeChromeMaterialBridge;

// Material owner for a Views bubble panel (command bar, developer tools).
//
// A bubble is its own NSWindow, so a compositor backdrop blur inside it has
// no page pixels to sample and only produces a flat, see-through sheet. On
// macOS 26 this places one NSGlassEffectView exactly behind the bubble's
// ClientView (the visible rounded panel) and paints only a light semantic
// veil in Views on top. Reduce Transparency, Increase Contrast, Glass OFF and
// power fallbacks yield the unchanged opaque panel and remove the native view.
//
// The owner is the bubble's contents view; it calls Apply() whenever the
// GlassPolicy changes and after the bubble widget exists. Geometry and
// light/dark changes of the ClientView are tracked automatically.
class NativePanelMaterial final : public views::ViewObserver {
 public:
  explicit NativePanelMaterial(SurfaceRole role);
  NativePanelMaterial(const NativePanelMaterial&) = delete;
  NativePanelMaterial& operator=(const NativePanelMaterial&) = delete;
  ~NativePanelMaterial() override;

  // Paints `contents` (before its widget exists) or its widget's ClientView
  // and keeps the native panel glass in sync with it.
  void Apply(views::View* contents, const GlassPolicy& policy);

  bool is_native_for_testing() const;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewThemeChanged(views::View* observed_view) override;
  void OnViewRemovedFromWidget(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void Detach();
  void SyncNativeMaterial();

  const SurfaceRole role_;
  GlassPolicy policy_;
  SurfaceAppearance surface_;
  raw_ptr<views::View> panel_ = nullptr;
  std::unique_ptr<NativeChromeMaterialBridge> bridge_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace ahoi::appearance

#endif  // AHOI_BROWSER_UI_APPEARANCE_NATIVE_PANEL_MATERIAL_H_
