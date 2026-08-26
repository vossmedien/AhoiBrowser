// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_cache_status_view.h"

#include <memory>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {

DeveloperCacheStatusView::DeveloperCacheStatusView(std::u16string site_label,
                                                   PrefService* prefs) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 3));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_CACHE_STATUS_TITLE));

  auto* title = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_CACHE_STATUS_TITLE),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4_MEDIUM));
  title->SetSubpixelRenderingEnabled(false);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetEnabledColor(visual_style::kText);

  auto* site = AddChildView(std::make_unique<views::Label>(site_label));
  site->SetSubpixelRenderingEnabled(false);
  site->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  site->SetElideBehavior(gfx::ELIDE_MIDDLE);
  site->SetEnabledColor(visual_style::kMutedText);

  status_label_ = AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetEnabledColor(visual_style::kMutedText);
  SetState(State::kClearing);

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs,
          base::BindRepeating(&DeveloperCacheStatusView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
}

DeveloperCacheStatusView::~DeveloperCacheStatusView() = default;

void DeveloperCacheStatusView::SetState(State state) {
  state_ = state;
  int string_id = IDS_AHOI_DEVELOPER_CACHE_STATUS_CLEARING;
  switch (state) {
    case State::kClearing:
      break;
    case State::kSucceeded:
      string_id = IDS_AHOI_DEVELOPER_CACHE_STATUS_DONE;
      break;
    case State::kFailed:
      string_id = IDS_AHOI_DEVELOPER_CACHE_STATUS_FAILED;
      break;
  }
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  GetViewAccessibility().AnnounceText(status_label_->GetText());
  PreferredSizeChanged();
}

void DeveloperCacheStatusView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

void DeveloperCacheStatusView::OnAppearanceChanged(
    const appearance::GlassPolicy& policy) {
  const appearance::SurfaceAppearance surface =
      appearance::AppearanceResolver::Resolve(
          appearance::SurfaceRole::kDeveloperTools, policy);
  views::ClientView* client_view =
      GetWidget() ? GetWidget()->client_view() : nullptr;
  if (!client_view) {
    appearance::ApplySurfaceAppearance(this, surface);
    return;
  }
  appearance::ClearSurfaceBackgroundAppearance(this);
  appearance::ApplySurfaceBackgroundAppearance(client_view, surface);
}

}  // namespace ahoi
