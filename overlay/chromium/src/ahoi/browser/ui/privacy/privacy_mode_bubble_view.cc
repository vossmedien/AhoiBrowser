// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/privacy/privacy_mode_bubble_view.h"

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "chrome/grit/generated_resources.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace ahoi {

namespace {

std::unique_ptr<ui::SimpleComboboxModel> MakeModel(
    std::initializer_list<int> string_ids) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  items.reserve(string_ids.size());
  for (const int string_id : string_ids) {
    items.emplace_back(l10n_util::GetStringUTF16(string_id));
  }
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}

std::unique_ptr<views::Label> MakeSectionLabel(int string_id) {
  auto label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(string_id), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  return label;
}

std::unique_ptr<views::Label> MakeDescriptionLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(true);
  label->SetEnabledColor(visual_style::kMutedText);
  return label;
}

int ModeDescriptionStringId(privacy::PrivacyMode mode) {
  return mode == privacy::PrivacyMode::kStrict
             ? IDS_AHOI_PRIVACY_MODE_MORE_PROTECTION_DESC
             : IDS_AHOI_PRIVACY_MODE_MAX_COMPATIBILITY_DESC;
}

void StyleCombobox(views::Combobox* combobox, int accessible_name_id) {
  combobox->SetAccessibleName(l10n_util::GetStringUTF16(accessible_name_id));
  combobox->SetBackgroundColorId(visual_style::kRaisedSurface);
  combobox->SetForegroundColorId(visual_style::kText);
  combobox->SetBorderColorId(visual_style::kDivider);
  combobox->SetPreferredSize(
      gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
}

}  // namespace

PrivacyModeBubbleView::PrivacyModeBubbleView(
    std::u16string origin_label,
    privacy::PrivacyMode global_mode,
    std::optional<privacy::PrivacyMode> origin_mode,
    bool global_mode_managed,
    bool site_controls_enabled,
    bool is_off_the_record,
    GlobalModeCallback global_mode_callback,
    OriginModeCallback origin_mode_callback)
    : site_controls_enabled_(site_controls_enabled),
      is_off_the_record_(is_off_the_record),
      global_mode_callback_(std::move(global_mode_callback)),
      origin_mode_callback_(std::move(origin_mode_callback)),
      global_mode_(global_mode),
      origin_mode_(origin_mode) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      visual_style::kDeveloperToolkitControlSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  SetAccessibleName(l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_TITLE));

  auto* title = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_TITLE),
      views::style::CONTEXT_DIALOG_TITLE, views::style::STYLE_HEADLINE_4));
  title->SetSubpixelRenderingEnabled(false);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetEnabledColor(visual_style::kText);

  auto* origin = AddChildView(std::make_unique<views::Label>(origin_label));
  origin->SetSubpixelRenderingEnabled(false);
  origin->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  origin->SetElideBehavior(gfx::ELIDE_MIDDLE);
  origin->SetEnabledColor(visual_style::kMutedText);

  auto* separator = AddChildView(std::make_unique<views::Separator>());
  separator->SetColorId(visual_style::kDivider);

  AddChildView(MakeSectionLabel(IDS_AHOI_PRIVACY_GLOBAL_MODE));
  auto global_mode_combobox = std::make_unique<views::Combobox>(MakeModel(
      {IDS_AHOI_PRIVACY_MODE_STRICT, IDS_AHOI_PRIVACY_MODE_COMPATIBLE}));
  global_mode_combobox->SetSelectedIndex(
      global_mode == privacy::PrivacyMode::kStrict ? 0u : 1u);
  global_mode_combobox->SetCallback(base::BindRepeating(
      &PrivacyModeBubbleView::OnGlobalModeChanged, base::Unretained(this)));
  StyleCombobox(global_mode_combobox.get(), IDS_AHOI_PRIVACY_GLOBAL_MODE);
  global_mode_combobox->SetEnabled(!global_mode_managed && !is_off_the_record);
  global_mode_combobox_ = AddChildView(std::move(global_mode_combobox));
  global_mode_description_ = AddChildView(MakeDescriptionLabel(u""));

  AddChildView(MakeDescriptionLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_SCOPE_HINT)));

  AddChildView(MakeSectionLabel(IDS_AHOI_PRIVACY_SITE_MODE));
  auto origin_mode_combobox = std::make_unique<views::Combobox>(
      MakeModel({IDS_AHOI_PRIVACY_SITE_INHERIT, IDS_AHOI_PRIVACY_MODE_STRICT,
                 IDS_AHOI_PRIVACY_MODE_COMPATIBLE}));
  const size_t origin_mode_index =
      !origin_mode.has_value()
          ? 0u
          : (*origin_mode == privacy::PrivacyMode::kStrict ? 1u : 2u);
  origin_mode_combobox->SetSelectedIndex(origin_mode_index);
  origin_mode_combobox->SetCallback(base::BindRepeating(
      &PrivacyModeBubbleView::OnOriginModeChanged, base::Unretained(this)));
  StyleCombobox(origin_mode_combobox.get(), IDS_AHOI_PRIVACY_SITE_MODE);
  origin_mode_combobox->SetEnabled(site_controls_enabled && !is_off_the_record);
  origin_mode_combobox_ = AddChildView(std::move(origin_mode_combobox));
  site_mode_description_ = AddChildView(MakeDescriptionLabel(u""));

  effective_mode_label_ = AddChildView(std::make_unique<views::Label>());
  effective_mode_label_->SetSubpixelRenderingEnabled(false);
  effective_mode_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  effective_mode_label_->SetMultiLine(true);
  effective_mode_label_->SetEnabledColor(visual_style::kMutedText);

  auto repair_button = std::make_unique<DeveloperToolkitButton>(
      base::BindRepeating(&PrivacyModeBubbleView::OnRepairCompatibility,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_REPAIR_SITE));
  repair_button->SetAccessibleName(std::u16string(repair_button->GetText()));
  repair_button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  repair_button_ = AddChildView(std::move(repair_button));

  status_label_ = AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetEnabledColor(visual_style::kMutedText);
  status_label_->SetVisible(false);

  UpdateEffectiveModeLabel();
  if (is_off_the_record_) {
    ShowStatus(IDS_AHOI_PRIVACY_INCOGNITO_HINT);
  } else if (!site_controls_enabled_) {
    ShowStatus(IDS_AHOI_PRIVACY_UNSUPPORTED_SITE);
  } else if (global_mode_managed) {
    ShowStatus(IDS_AHOI_PRIVACY_MANAGED_HINT);
  }
}

PrivacyModeBubbleView::~PrivacyModeBubbleView() = default;

void PrivacyModeBubbleView::OnGlobalModeChanged() {
  if (synchronizing_controls_ || is_off_the_record_) {
    return;
  }
  const privacy::PrivacyMode requested =
      global_mode_combobox_->GetSelectedIndex().value_or(0) == 0
          ? privacy::PrivacyMode::kStrict
          : privacy::PrivacyMode::kChromiumCompatible;
  if (!global_mode_callback_.Run(requested)) {
    base::AutoReset<bool> synchronizing(&synchronizing_controls_, true);
    global_mode_combobox_->SetSelectedIndex(
        global_mode_ == privacy::PrivacyMode::kStrict ? 0u : 1u);
    UpdateEffectiveModeLabel();
    ShowStatus(IDS_AHOI_PRIVACY_UPDATE_FAILED);
    return;
  }
  global_mode_ = requested;
  UpdateEffectiveModeLabel();
  ShowStatus(IDS_AHOI_PRIVACY_UPDATED_RELOAD);
}

void PrivacyModeBubbleView::OnOriginModeChanged() {
  if (synchronizing_controls_ || !site_controls_enabled_ ||
      is_off_the_record_) {
    return;
  }
  const size_t selected = origin_mode_combobox_->GetSelectedIndex().value_or(0);
  std::optional<privacy::PrivacyMode> requested;
  if (selected == 1) {
    requested = privacy::PrivacyMode::kStrict;
  } else if (selected == 2) {
    requested = privacy::PrivacyMode::kChromiumCompatible;
  }
  if (!origin_mode_callback_.Run(requested)) {
    base::AutoReset<bool> synchronizing(&synchronizing_controls_, true);
    origin_mode_combobox_->SetSelectedIndex(
        !origin_mode_.has_value()
            ? 0u
            : (*origin_mode_ == privacy::PrivacyMode::kStrict ? 1u : 2u));
    UpdateEffectiveModeLabel();
    ShowStatus(IDS_AHOI_PRIVACY_UPDATE_FAILED);
    return;
  }
  origin_mode_ = requested;
  UpdateEffectiveModeLabel();
  ShowStatus(IDS_AHOI_PRIVACY_UPDATED_RELOAD);
}

void PrivacyModeBubbleView::OnRepairCompatibility() {
  if (synchronizing_controls_ || !site_controls_enabled_ ||
      is_off_the_record_) {
    return;
  }
  if (!origin_mode_callback_.Run(privacy::PrivacyMode::kChromiumCompatible)) {
    ShowStatus(IDS_AHOI_PRIVACY_UPDATE_FAILED);
    return;
  }
  origin_mode_ = privacy::PrivacyMode::kChromiumCompatible;
  {
    base::AutoReset<bool> synchronizing(&synchronizing_controls_, true);
    origin_mode_combobox_->SetSelectedIndex(2u);
  }
  UpdateEffectiveModeLabel();
  ShowStatus(IDS_AHOI_PRIVACY_REPAIR_DONE);
}

void PrivacyModeBubbleView::UpdateEffectiveModeLabel() {
  const privacy::PrivacyMode effective = origin_mode_.value_or(global_mode_);
  const std::u16string effective_format =
      l10n_util::GetStringUTF16(IDS_AHOI_PRIVACY_EFFECTIVE_MODE);
  if (effective_format.find(u"$1") == std::u16string::npos) {
    // Views-only unit tests load the UI test pak, not Chrome's generated
    // locale pack. Numeric resource IDs can therefore resolve to an unrelated
    // UI string; avoid formatting unless the expected placeholder is present.
    effective_mode_label_->SetText(u"");
  } else {
    effective_mode_label_->SetText(l10n_util::GetStringFUTF16(
        IDS_AHOI_PRIVACY_EFFECTIVE_MODE,
        l10n_util::GetStringUTF16(effective == privacy::PrivacyMode::kStrict
                                      ? IDS_AHOI_PRIVACY_MODE_STRICT
                                      : IDS_AHOI_PRIVACY_MODE_COMPATIBLE)));
  }
  global_mode_description_->SetText(
      l10n_util::GetStringUTF16(ModeDescriptionStringId(global_mode_)));
  site_mode_description_->SetText(l10n_util::GetStringUTF16(
      origin_mode_.has_value() ? ModeDescriptionStringId(effective)
                               : IDS_AHOI_PRIVACY_SITE_INHERIT_DESC));
  repair_button_->SetVisible(site_controls_enabled_ && !is_off_the_record_ &&
                             effective !=
                                 privacy::PrivacyMode::kChromiumCompatible);
  PreferredSizeChanged();
}

void PrivacyModeBubbleView::ShowStatus(int string_id) {
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

}  // namespace ahoi
