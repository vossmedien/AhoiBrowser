// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_dialog.h"

#include <memory>
#include <string>

#include "ahoi/browser/extensions/ubo_service_factory.h"
#include "ahoi/browser/ui/extensions/ubo_ui_tokens.h"
#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::extensions {

namespace {

std::unique_ptr<views::Label> MakeBodyLabel(int string_id) {
  auto label =
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(string_id),
                                     views::style::CONTEXT_DIALOG_BODY_TEXT);
  label->SetSubpixelRenderingEnabled(false);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::u16string MetadataText(int label_id, std::string_view value) {
  return base::StrCat(
      {l10n_util::GetStringUTF16(label_id), u": ", base::UTF8ToUTF16(value)});
}

}  // namespace

UboInstallDialog::UboInstallDialog(Browser* browser, UboService* service)
    : browser_(browser), service_(service) {
  CHECK(browser_);
  CHECK(service_);
  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
  set_fixed_width(ui_tokens::kDialogWidth);
  set_margins(views::LayoutProvider::Get()->GetInsetsMetric(
      views::InsetsMetric::INSETS_DIALOG));

  views::View* contents = SetContentsView(std::make_unique<views::View>());
  auto* layout =
      contents->SetLayoutManager(std::make_unique<views::BoxLayout>());
  layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->set_between_child_spacing(ui_tokens::kSectionSpacing);

  status_label_ = contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_IDLE));

  progress_ = contents->AddChildView(std::make_unique<views::ProgressBar>());
  progress_->SetPreferredHeight(ui_tokens::kProgressHeight);
  progress_->SetValue(-1);

  metadata_ = contents->AddChildView(std::make_unique<views::View>());
  auto* metadata_layout =
      metadata_->SetLayoutManager(std::make_unique<views::BoxLayout>());
  metadata_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  metadata_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  metadata_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);
  version_ = metadata_->AddChildView(std::make_unique<views::Label>());
  extension_id_ = metadata_->AddChildView(std::make_unique<views::Label>());
  upstream_ = metadata_->AddChildView(std::make_unique<views::Label>());
  hash_ = metadata_->AddChildView(std::make_unique<views::Label>());
  license_ = metadata_->AddChildView(std::make_unique<views::Label>());
  for (views::Label* label : {version_.get(), extension_id_.get(),
                              upstream_.get(), hash_.get(), license_.get()}) {
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMultiLine(true);
    label->SetSelectable(true);
  }

  contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_SECURITY_NOTICE));
  contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_GPL_NOTICE));
  contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_CONFIRM_NOTICE));

  service_->AddObserver(this);
  Update(service_->status());
}

UboInstallDialog::~UboInstallDialog() {
  if (service_) {
    service_->RemoveObserver(this);
  }
}

std::u16string UboInstallDialog::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_AHOI_UBO_TITLE);
}

bool UboInstallDialog::Accept() {
  switch (action_) {
    case UboDialogAction::kCheck:
      service_->CheckForCatalog(UboCheckReason::kManual);
      return false;
    case UboDialogAction::kDownload:
      service_->PreparePackage();
      return false;
    case UboDialogAction::kInstall:
      service_->InstallPreparedPackage(
          browser_->tab_strip_model()->GetActiveWebContents());
      return false;
    case UboDialogAction::kClose:
      return true;
    case UboDialogAction::kNone:
      return false;
  }
}

void UboInstallDialog::OnUboServiceStatusChanged(
    const UboServiceStatus& status) {
  Update(status);
}

void UboInstallDialog::Update(const UboServiceStatus& status) {
  UboDialogPresentation presentation = PresentUboStatus(status);
  action_ = presentation.action;
  status_label_->SetText(
      l10n_util::GetStringUTF16(presentation.status_string_id));
  progress_->SetVisible(presentation.show_progress);
  metadata_->SetVisible(presentation.show_metadata);
  SetButtonLabel(
      ui::mojom::DialogButton::kOk,
      l10n_util::GetStringUTF16(presentation.primary_button_string_id));
  SetButtonEnabled(ui::mojom::DialogButton::kOk, presentation.primary_enabled);

  if (status.catalog) {
    version_->SetText(MetadataText(IDS_AHOI_UBO_VERSION_LABEL,
                                   status.catalog->version.GetString()));
    extension_id_->SetText(
        MetadataText(IDS_AHOI_UBO_ID_LABEL, status.catalog->extension_id));
    upstream_->SetText(MetadataText(
        IDS_AHOI_UBO_UPSTREAM_LABEL,
        base::StrCat({status.catalog->upstream_tag, " — ",
                      status.catalog->upstream_source_url.spec()})));
    hash_->SetText(
        MetadataText(IDS_AHOI_UBO_HASH_LABEL, status.catalog->package_sha256));
    license_->SetText(
        MetadataText(IDS_AHOI_UBO_LICENSE_LABEL, status.catalog->license));
  }
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
  }
}

void ShowUboInstallDialog(Browser* browser) {
  if (!browser || !browser->GetProfile() ||
      !browser->GetProfile()->IsRegularProfile() || !browser->GetWindow()) {
    return;
  }
  UboService* service = UboServiceFactory::GetForProfile(browser->GetProfile());
  if (!service) {
    return;
  }
  constrained_window::CreateBrowserModalDialogViews(
      std::make_unique<UboInstallDialog>(browser, service),
      browser->GetWindow()->GetNativeWindow())
      ->Show();
}

}  // namespace ahoi::extensions
