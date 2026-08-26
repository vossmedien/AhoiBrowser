// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_bubble_view.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/ui/appearance/appearance_runtime_signals.h"
#include "ahoi/browser/ui/appearance/appearance_views.h"
#include "ahoi/browser/ui/developer_toolkit/developer_data_clear_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_toolkit_button.h"
#include "ahoi/browser/ui/visual_style.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {

namespace {

const gfx::VectorIcon& ActionIcon(DeveloperAction action) {
  switch (action) {
    case DeveloperAction::kClearCache:
    case DeveloperAction::kResetDocumentModifications:
      return vector_icons::kRefreshIcon;
    case DeveloperAction::kClearSiteData:
      return vector_icons::kCookieIcon;
    case DeveloperAction::kToggleCss:
    case DeveloperAction::kToggleJavaScript:
    case DeveloperAction::kToggleStructureOutlines:
    case DeveloperAction::kToggleDocumentMetadata:
      return vector_icons::kCodeIcon;
    case DeveloperAction::kTogglePasswordFields:
    case DeveloperAction::kToggleAltTitleLabels:
      return views::kVisibilityIcon;
    case DeveloperAction::kToggleImages:
      return vector_icons::kPhotoIcon;
    case DeveloperAction::kCaptureVisibleScreenshot:
    case DeveloperAction::kCaptureFullPageScreenshot:
      return vector_icons::kPhotoIcon;
  }
}

int ActionStringId(DeveloperAction action) {
  switch (action) {
    case DeveloperAction::kClearCache:
      return IDS_AHOI_DEVELOPER_CLEAR_SITE_CACHE;
    case DeveloperAction::kClearSiteData:
      return IDS_AHOI_DEVELOPER_CLEAR_SITE_DATA;
    case DeveloperAction::kToggleCss:
      return IDS_AHOI_DEVELOPER_TOGGLE_CSS;
    case DeveloperAction::kTogglePasswordFields:
      return IDS_AHOI_DEVELOPER_TOGGLE_PASSWORD_FIELDS;
    case DeveloperAction::kToggleStructureOutlines:
      return IDS_AHOI_DEVELOPER_TOGGLE_STRUCTURE_OUTLINES;
    case DeveloperAction::kToggleAltTitleLabels:
      return IDS_AHOI_DEVELOPER_TOGGLE_ALT_TITLE;
    case DeveloperAction::kToggleDocumentMetadata:
      return IDS_AHOI_DEVELOPER_TOGGLE_METADATA;
    case DeveloperAction::kResetDocumentModifications:
      return IDS_AHOI_DEVELOPER_RESET_DOCUMENT;
    case DeveloperAction::kToggleJavaScript:
      return IDS_AHOI_DEVELOPER_TOGGLE_JAVASCRIPT;
    case DeveloperAction::kToggleImages:
      return IDS_AHOI_DEVELOPER_TOGGLE_IMAGES;
    case DeveloperAction::kCaptureVisibleScreenshot:
      return IDS_AHOI_DEVELOPER_SCREENSHOT_VISIBLE;
    case DeveloperAction::kCaptureFullPageScreenshot:
      return IDS_AHOI_DEVELOPER_SCREENSHOT_FULL_PAGE;
  }
}

std::optional<DeveloperActivation> ActivationForAction(DeveloperAction action) {
  switch (action) {
    case DeveloperAction::kToggleCss:
      return DeveloperActivation::kCss;
    case DeveloperAction::kTogglePasswordFields:
      return DeveloperActivation::kPasswordFields;
    case DeveloperAction::kToggleStructureOutlines:
      return DeveloperActivation::kStructureOutlines;
    case DeveloperAction::kToggleAltTitleLabels:
      return DeveloperActivation::kAltTitleLabels;
    case DeveloperAction::kToggleDocumentMetadata:
      return DeveloperActivation::kDocumentMetadata;
    case DeveloperAction::kToggleJavaScript:
      return DeveloperActivation::kJavaScript;
    case DeveloperAction::kToggleImages:
      return DeveloperActivation::kImages;
    case DeveloperAction::kClearCache:
    case DeveloperAction::kClearSiteData:
    case DeveloperAction::kResetDocumentModifications:
    case DeveloperAction::kCaptureVisibleScreenshot:
    case DeveloperAction::kCaptureFullPageScreenshot:
      return std::nullopt;
  }
}

std::unique_ptr<views::Label> CreateSectionLabel(int string_id) {
  auto label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(string_id), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  return label;
}

}  // namespace

DeveloperToolkitBubbleView::DeveloperToolkitBubbleView(
    std::u16string origin_label,
    developer_toolkit_prefs::ToolbarVisibility visibility,
    ExecuteCallback execute_callback,
    DataClearCallback data_clear_callback,
    VisibilityCallback visibility_callback,
    OpenDevToolsCallback open_devtools_callback,
    OpenCookieManagerCallback open_cookie_manager_callback,
    OpenProfileCallback open_profile_callback,
    PrefService* prefs,
    DeveloperActivationState initial_activation)
    : execute_callback_(std::move(execute_callback)),
      data_clear_callback_(std::move(data_clear_callback)),
      visibility_callback_(std::move(visibility_callback)),
      open_devtools_callback_(std::move(open_devtools_callback)),
      open_cookie_manager_callback_(std::move(open_cookie_manager_callback)),
      open_profile_callback_(std::move(open_profile_callback)),
      activation_state_(initial_activation) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      visual_style::kDeveloperToolkitControlSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLKIT_TITLE));

  auto* title = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLKIT_TITLE),
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

  activation_label_ = AddChildView(std::make_unique<views::Label>());
  activation_label_->SetSubpixelRenderingEnabled(false);
  activation_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  activation_label_->SetEnabledColor(visual_style::kAccent);
  activation_label_->SetMultiLine(true);
  UpdateActivationChips();

  auto primary_row = std::make_unique<views::View>();
  auto* primary_layout =
      primary_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          visual_style::kDeveloperToolkitControlSpacing));
  auto devtools_button = std::make_unique<DeveloperToolkitButton>(
      open_devtools_callback_,
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_OPEN_DEVTOOLS),
      &vector_icons::kCodeIcon);
  devtools_button->SetAccessibleName(
      std::u16string(devtools_button->GetText()));
  devtools_button_ = primary_row->AddChildView(std::move(devtools_button));
  auto profile_button = std::make_unique<DeveloperToolkitButton>(
      open_profile_callback_,
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_TITLE),
      &vector_icons::kCodeIcon);
  profile_button->SetAccessibleName(std::u16string(profile_button->GetText()));
  views::LabelButton* const profile_button_ptr =
      primary_row->AddChildView(std::move(profile_button));
  primary_layout->SetFlexForView(devtools_button_, 1);
  primary_layout->SetFlexForView(profile_button_ptr, 1);
  AddChildView(std::move(primary_row));

  AddChildView(CreateSectionLabel(IDS_AHOI_DEVELOPER_SECTION_PAGE));
  AddChildView(CreateActionRow(DeveloperAction::kToggleCss,
                               DeveloperAction::kTogglePasswordFields));
  AddChildView(CreateActionRow(DeveloperAction::kToggleJavaScript,
                               DeveloperAction::kToggleImages));
  AddChildView(CreateActionRow(DeveloperAction::kToggleStructureOutlines,
                               DeveloperAction::kToggleAltTitleLabels));
  auto metadata_row = std::make_unique<views::View>();
  auto* metadata_layout =
      metadata_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  views::LabelButton* metadata = AddActionButton(
      metadata_row.get(), DeveloperAction::kToggleDocumentMetadata);
  metadata_layout->SetFlexForView(metadata, 1);
  AddChildView(std::move(metadata_row));
  auto reset_row = std::make_unique<views::View>();
  auto* reset_layout =
      reset_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  views::LabelButton* reset = AddActionButton(
      reset_row.get(), DeveloperAction::kResetDocumentModifications);
  reset_layout->SetFlexForView(reset, 1);
  AddChildView(std::move(reset_row));

  AddChildView(CreateSectionLabel(IDS_AHOI_DEVELOPER_SECTION_DATA));
  auto cookie_row = std::make_unique<views::View>();
  auto* cookie_layout =
      cookie_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  auto cookie_button = std::make_unique<DeveloperToolkitButton>(
      open_cookie_manager_callback_,
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_COOKIE_MANAGER_OPEN),
      &vector_icons::kCookieIcon);
  cookie_button->SetAccessibleName(std::u16string(cookie_button->GetText()));
  views::LabelButton* const cookie_button_ptr =
      cookie_row->AddChildView(std::move(cookie_button));
  cookie_layout->SetFlexForView(cookie_button_ptr, 1);
  AddChildView(std::move(cookie_row));
  AddChildView(std::make_unique<DeveloperDataClearView>(data_clear_callback_));

  AddChildView(CreateSectionLabel(IDS_AHOI_DEVELOPER_SECTION_CAPTURE));
  AddChildView(CreateActionRow(DeveloperAction::kCaptureVisibleScreenshot,
                               DeveloperAction::kCaptureFullPageScreenshot));

  AddChildView(CreateSectionLabel(IDS_AHOI_DEVELOPER_SECTION_TOOLBAR));
  auto toolbar_options = std::make_unique<views::View>();
  auto* toolbar_layout =
      toolbar_options->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  cookie_visibility_ =
      toolbar_options->AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLBAR_COOKIES),
          base::BindRepeating(
              &DeveloperToolkitBubbleView::OnToolbarVisibilityChanged,
              base::Unretained(this))));
  cache_visibility_ =
      toolbar_options->AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLBAR_CACHE),
          base::BindRepeating(
              &DeveloperToolkitBubbleView::OnToolbarVisibilityChanged,
              base::Unretained(this))));
  toolkit_visibility_ =
      toolbar_options->AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_TOOLBAR_HELPERS),
          base::BindRepeating(
              &DeveloperToolkitBubbleView::OnToolbarVisibilityChanged,
              base::Unretained(this))));
  cookie_visibility_->SetTextSubpixelRenderingEnabled(false);
  cache_visibility_->SetTextSubpixelRenderingEnabled(false);
  toolkit_visibility_->SetTextSubpixelRenderingEnabled(false);
  cookie_visibility_->SetChecked(visibility.cookie);
  cache_visibility_->SetChecked(visibility.cache);
  toolkit_visibility_->SetChecked(visibility.toolkit);
  toolbar_layout->SetFlexForView(cookie_visibility_, 1);
  toolbar_layout->SetFlexForView(cache_visibility_, 1);
  toolbar_layout->SetFlexForView(toolkit_visibility_, 1);
  AddChildView(std::move(toolbar_options));

  status_label_ = AddChildView(std::make_unique<views::Label>());
  status_label_->SetSubpixelRenderingEnabled(false);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);
  status_label_->SetEnabledColor(visual_style::kMutedText);
  status_label_->SetVisible(false);

  appearance_signal_source_ =
      std::make_unique<appearance::AppearanceRuntimeSignalSource>(
          prefs,
          base::BindRepeating(&DeveloperToolkitBubbleView::OnAppearanceChanged,
                              weak_ptr_factory_.GetWeakPtr()));
  OnAppearanceChanged(appearance_signal_source_->policy());
}

DeveloperToolkitBubbleView::~DeveloperToolkitBubbleView() = default;

void DeveloperToolkitBubbleView::ReapplyAppearance() {
  OnAppearanceChanged(appearance_signal_source_->policy());
}

views::LabelButton* DeveloperToolkitBubbleView::action_button_for_testing(
    DeveloperAction action) const {
  const auto found = action_buttons_.find(action);
  return found == action_buttons_.end() ? nullptr : found->second.get();
}

std::unique_ptr<views::View> DeveloperToolkitBubbleView::CreateActionRow(
    DeveloperAction first,
    DeveloperAction second) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      visual_style::kDeveloperToolkitControlSpacing));
  views::LabelButton* first_button = AddActionButton(row.get(), first);
  views::LabelButton* second_button = AddActionButton(row.get(), second);
  layout->SetFlexForView(first_button, 1);
  layout->SetFlexForView(second_button, 1);
  return row;
}

views::LabelButton* DeveloperToolkitBubbleView::AddActionButton(
    views::View* parent,
    DeveloperAction action) {
  auto button = std::make_unique<DeveloperToolkitButton>(
      base::BindRepeating(&DeveloperToolkitBubbleView::OnActionPressed,
                          base::Unretained(this), action),
      l10n_util::GetStringUTF16(ActionStringId(action)), &ActionIcon(action));
  button->SetAccessibleName(std::u16string(button->GetText()));
  views::LabelButton* const button_ptr =
      parent->AddChildView(std::move(button));
  action_buttons_.emplace(action, button_ptr);
  if (const std::optional<DeveloperActivation> activation =
          ActivationForAction(action)) {
    static_cast<DeveloperToolkitButton*>(button_ptr)
        ->SetSelected(activation_state_.Has(*activation));
  }
  return button_ptr;
}

void DeveloperToolkitBubbleView::OnActionPressed(DeveloperAction action) {
  const DeveloperActionResult result = execute_callback_.Run(action);
  switch (result.status) {
    case DeveloperActionStatus::kExecuted:
      if (action == DeveloperAction::kResetDocumentModifications) {
        activation_state_.Reset();
        for (const auto& [mapped_action, button] : action_buttons_) {
          if (ActivationForAction(mapped_action)) {
            static_cast<DeveloperToolkitButton*>(button.get())
                ->SetSelected(false);
          }
        }
      } else if (const std::optional<DeveloperActivation> activation =
                     ActivationForAction(action)) {
        activation_state_.Toggle(*activation);
        static_cast<DeveloperToolkitButton*>(action_buttons_.at(action).get())
            ->SetSelected(activation_state_.Has(*activation));
      }
      UpdateActivationChips();
      ShowStatus(action == DeveloperAction::kToggleJavaScript ||
                         action == DeveloperAction::kToggleImages
                     ? IDS_AHOI_DEVELOPER_ACTION_RELOAD_REQUIRED
                     : IDS_AHOI_DEVELOPER_ACTION_DONE);
      return;
    case DeveloperActionStatus::kRejectedUnsupportedTarget:
      ShowStatus(IDS_AHOI_DEVELOPER_UNSUPPORTED_PAGE);
      return;
    case DeveloperActionStatus::kUnavailable:
      ShowStatus(IDS_AHOI_DEVELOPER_ACTION_UNAVAILABLE);
      return;
  }
}

void DeveloperToolkitBubbleView::UpdateActivationChips() {
  struct Chip {
    DeveloperActivation activation;
    std::u16string_view label;
  };
  constexpr Chip kChips[] = {
      {DeveloperActivation::kCss, u"CSS"},
      {DeveloperActivation::kJavaScript, u"JS"},
      {DeveloperActivation::kImages, u"IMG OFF"},
      {DeveloperActivation::kHeaders, u"HDR"},
      {DeveloperActivation::kCacheOff, u"CACHE OFF"},
      {DeveloperActivation::kPasswordFields, u"PASSWORD"},
      {DeveloperActivation::kStructureOutlines, u"OUTLINE"},
      {DeveloperActivation::kAltTitleLabels, u"ALT/TITLE"},
      {DeveloperActivation::kDocumentMetadata, u"META"},
  };
  std::u16string text;
  for (const Chip& chip : kChips) {
    if (!activation_state_.Has(chip.activation)) {
      continue;
    }
    if (!text.empty()) {
      text.append(u" · ");
    }
    text.append(chip.label);
  }
  activation_label_->SetText(text);
  activation_label_->SetVisible(!text.empty());
  PreferredSizeChanged();
}

void DeveloperToolkitBubbleView::OnToolbarVisibilityChanged() {
  developer_toolkit_prefs::ToolbarVisibility visibility{
      .cookie = cookie_visibility_->GetChecked(),
      .cache = cache_visibility_->GetChecked(),
      .toolkit = toolkit_visibility_->GetChecked(),
  };
  if (visibility_callback_.Run(visibility)) {
    ShowStatus(IDS_AHOI_DEVELOPER_TOOLBAR_UPDATED);
    return;
  }
  toolkit_visibility_->SetChecked(true);
  ShowStatus(IDS_AHOI_DEVELOPER_TOOLBAR_ONE_REQUIRED);
}

void DeveloperToolkitBubbleView::ShowStatus(int string_id) {
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  status_label_->SetVisible(true);
  PreferredSizeChanged();
}

void DeveloperToolkitBubbleView::OnAppearanceChanged(
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
