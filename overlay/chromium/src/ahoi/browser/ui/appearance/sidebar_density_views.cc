// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/appearance/sidebar_density_views.h"

#include "ui/base/class_property.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"

namespace ahoi::appearance {
namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(int, kDensity, -1)
DEFINE_UI_CLASS_PROPERTY_KEY(int, kAppliedFontDelta, 0)

void InvalidateSubtree(views::View* view) {
  for (views::View* child : view->children()) {
    InvalidateSubtree(child);
  }
  view->InvalidateLayout();
  view->SchedulePaint();
}

}  // namespace

SidebarDensityMetrics GetSidebarDensityMetricsForView(const views::View* view) {
  for (; view; view = view->parent()) {
    const int value = view->GetProperty(kDensity);
    if (value != -1) {
      return GetSidebarDensityMetrics(NormalizeSidebarDensity(value));
    }
  }
  return GetSidebarDensityMetrics(SidebarDensity::kStandard);
}

void SetSidebarDensityForView(views::View* root, SidebarDensity density) {
  const int value = static_cast<int>(density);
  if (root->GetProperty(kDensity) == value) {
    return;
  }
  root->SetProperty(kDensity, value);
  // Only materialized Views are visited, never the durable tab tree. Keep
  // native focus, rename editors and AX nodes alive during a live change.
  InvalidateSubtree(root);
}

void ApplySidebarDensityFont(views::Label* label) {
  const int delta = GetSidebarDensityMetricsForView(label).font_size_delta;
  const int previous_delta = label->GetProperty(kAppliedFontDelta);
  if (delta != previous_delta) {
    label->SetFontList(
        label->font_list().DeriveWithSizeDelta(delta - previous_delta));
    label->SetProperty(kAppliedFontDelta, delta);
  }
}

void ApplySidebarDensityFont(views::Textfield* editor) {
  const int delta = GetSidebarDensityMetricsForView(editor).font_size_delta;
  const int previous_delta = editor->GetProperty(kAppliedFontDelta);
  if (delta != previous_delta) {
    editor->SetFontList(
        editor->GetFontList().DeriveWithSizeDelta(delta - previous_delta));
    editor->SetProperty(kAppliedFontDelta, delta);
  }
}

}  // namespace ahoi::appearance
