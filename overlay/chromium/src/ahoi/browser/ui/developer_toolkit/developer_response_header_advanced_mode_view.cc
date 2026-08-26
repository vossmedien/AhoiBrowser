// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_response_header_advanced_mode_view.h"

#include <memory>

#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace ahoi {

DeveloperResponseHeaderAdvancedModeView::
    DeveloperResponseHeaderAdvancedModeView(bool response_headers_enabled,
                                            bool acknowledged) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto warning = std::make_unique<views::Label>(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_PROFILE_ADVANCED_RESPONSE_HEADERS_WARNING));
  warning->SetSubpixelRenderingEnabled(false);
  warning->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  warning->SetMultiLine(true);
  warning->SetEnabledColor(ui::kColorAlertHighSeverity);
  warning_ = AddChildView(std::move(warning));

  acknowledgement_ =
      AddChildView(std::make_unique<views::Checkbox>(l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_PROFILE_ADVANCED_RESPONSE_HEADERS_ACKNOWLEDGE)));
  acknowledgement_->SetTextSubpixelRenderingEnabled(false);
  acknowledgement_->SetChecked(response_headers_enabled && acknowledged);
  acknowledgement_->SetEnabled(response_headers_enabled);
  SetVisible(response_headers_enabled);
}

DeveloperResponseHeaderAdvancedModeView::
    ~DeveloperResponseHeaderAdvancedModeView() = default;

void DeveloperResponseHeaderAdvancedModeView::SetResponseHeadersEnabled(
    bool enabled) {
  acknowledgement_->SetEnabled(enabled);
  if (!enabled) {
    acknowledgement_->SetChecked(false);
  }
  SetVisible(enabled);
  PreferredSizeChanged();
}

bool DeveloperResponseHeaderAdvancedModeView::acknowledged() const {
  return acknowledgement_->GetChecked();
}

}  // namespace ahoi
