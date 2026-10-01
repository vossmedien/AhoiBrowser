// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/developer_toolkit/developer_profile_editor_view.h"

#include <memory>
#include <utility>
#include <vector>

#include "ahoi/browser/developer_toolkit/developer_profile_text_codec.h"
#include "ahoi/browser/developer_toolkit/developer_style_compiler_service_client.h"
#include "ahoi/browser/ui/developer_toolkit/developer_header_secret_editor_view.h"
#include "ahoi/browser/ui/developer_toolkit/developer_response_header_advanced_mode_view.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textarea/textarea.h"

namespace ahoi {
namespace {
constexpr int kHeadersAreaHeight = 76;
std::unique_ptr<views::Label> CreateMutedLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(visual_style::kMutedText);
  label->SetMultiLine(true);
  return label;
}
}  // namespace

void DeveloperProfileEditorView::InitializeHeaderControls(
    const DeveloperProfile& initial_profile,
    content::WebContents* source_web_contents,
    DeveloperSecretStoreFactory secret_store_factory) {
  const bool is_off_the_record =
      !source_web_contents || !source_web_contents->GetBrowserContext() ||
      source_web_contents->GetBrowserContext()->IsOffTheRecord();
  auto header_secret_editor = std::make_unique<DeveloperHeaderSecretEditorView>(
      is_off_the_record, initial_profile.header_rules,
      initial_profile.response_header_rules, std::move(secret_store_factory),
      base::BindRepeating(&DeveloperProfileEditorView::ShowStatus,
                          weak_ptr_factory_.GetWeakPtr()));

  header_rules_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_HEADERS)));
  header_rules_enabled_->SetTextSubpixelRenderingEnabled(false);
  header_rules_enabled_->SetChecked(initial_profile.header_rules_enabled);
  header_rules_sync_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS)));
  header_rules_sync_enabled_->SetChecked(
      initial_profile.header_rules_sync_enabled);
  header_rules_ =
      AddTextControl(std::make_unique<views::Textarea>(), kHeadersAreaHeight,
                     std::u16string(header_rules_enabled_->GetText()),
                     l10n_util::GetStringUTF16(
                         IDS_AHOI_DEVELOPER_PROFILE_HEADERS_PLACEHOLDER));
  header_rules_->SetText(base::UTF8ToUTF16(
      FormatDeveloperHeaderRules(header_secret_editor->PlainRulesForEditor(
          DeveloperHeaderSecretDirection::kRequest))));

  response_header_rules_enabled_ =
      AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(
              IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS),
          base::BindRepeating(
              &DeveloperProfileEditorView::OnResponseHeaderRulesEnabledChanged,
              base::Unretained(this))));
  response_header_rules_enabled_->SetTextSubpixelRenderingEnabled(false);
  response_header_rules_enabled_->SetChecked(
      initial_profile.response_header_rules_enabled);
  response_header_rules_sync_enabled_ =
      AddChildView(std::make_unique<views::Checkbox>(
          l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_PROFILE_SYNC_HEADERS)));
  response_header_rules_sync_enabled_->SetChecked(
      initial_profile.response_header_rules_sync_enabled);
  response_header_rules_ = AddTextControl(
      std::make_unique<views::Textarea>(), kHeadersAreaHeight,
      std::u16string(response_header_rules_enabled_->GetText()),
      l10n_util::GetStringUTF16(
          IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS_PLACEHOLDER));
  response_header_rules_->SetText(base::UTF8ToUTF16(
      FormatDeveloperHeaderRules(header_secret_editor->PlainRulesForEditor(
          DeveloperHeaderSecretDirection::kResponse))));
  AddChildView(CreateMutedLabel(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_PROFILE_RESPONSE_HEADERS_HELP)));
  response_header_advanced_mode_ =
      AddChildView(std::make_unique<DeveloperResponseHeaderAdvancedModeView>(
          initial_profile.response_header_rules_enabled,
          initial_profile.response_header_advanced_mode_acknowledged));

  header_secret_editor_ = AddChildView(std::move(header_secret_editor));

  AddChildView(CreateMutedLabel(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_HEADER_LIFETIME)));
  std::vector<ui::SimpleComboboxModel::Item> lifetimes;
  lifetimes.emplace_back(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_HEADER_LIFETIME_TAB));
  lifetimes.emplace_back(
      l10n_util::GetStringUTF16(IDS_AHOI_DEVELOPER_ASSET_LIFETIME_RESTART));
  auto lifetime = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(lifetimes)));
  lifetime->SetSelectedIndex(initial_profile.headers_persistent ? 1 : 0);
  lifetime->SetAccessibleName(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_HEADER_LIFETIME));
  lifetime->SetBackgroundColorId(visual_style::kRaisedSurface);
  lifetime->SetForegroundColorId(visual_style::kText);
  lifetime->SetBorderColorId(visual_style::kDivider);
  lifetime->SetPreferredSize(gfx::Size(0, visual_style::kDeveloperToolkitRowHeight));
  lifetime->SetCallback(base::BindRepeating(
      &DeveloperProfileEditorView::OnHeaderLifetimeChanged,
      base::Unretained(this)));
  headers_lifetime_ = AddChildView(std::move(lifetime));
  AddChildView(CreateMutedLabel(l10n_util::GetStringUTF16(
      IDS_AHOI_DEVELOPER_HEADER_LIFETIME_HELP)));
  OnHeaderLifetimeChanged();
}

void DeveloperProfileEditorView::OnHeaderLifetimeChanged() {
  if (compile_in_flight_) {
    ++compile_generation_;
    compile_in_flight_ = false;
    style_compiler_->CloseEditor();
    header_secret_editor_->CompleteProfileCommit(false);
    if (toolkit_active_) {
      style_compiler_->OpenEditor();
    }
  }
  const bool persistent = headers_lifetime_->GetSelectedIndex() == 1;
  if (!persistent) {
    header_rules_sync_enabled_->SetChecked(false);
    response_header_rules_sync_enabled_->SetChecked(false);
  }
  header_rules_sync_enabled_->SetEnabled(persistent);
  response_header_rules_sync_enabled_->SetEnabled(persistent);
}

}  // namespace ahoi
