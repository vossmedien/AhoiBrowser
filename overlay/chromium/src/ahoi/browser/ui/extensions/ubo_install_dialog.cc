// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_dialog.h"

#include <memory>
#include <string>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "ahoi/browser/extensions/ubo_service_factory.h"
#include "ahoi/browser/ui/extensions/ubo_ui_tokens.h"
#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
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

std::u16string InventoryStateText(const UboExtensionState& state) {
  if (!state.installed) {
    return l10n_util::GetStringUTF16(IDS_AHOI_UBO_INVENTORY_NOT_INSTALLED);
  }
  return base::StrCat(
      {l10n_util::GetStringUTF16(IDS_AHOI_UBO_INVENTORY_INSTALLED), u" ",
       base::UTF8ToUTF16(state.version), u", ",
       l10n_util::GetStringUTF16(state.enabled
                                     ? IDS_AHOI_UBO_INVENTORY_ENABLED
                                     : IDS_AHOI_UBO_INVENTORY_DISABLED),
       u", ",
       l10n_util::GetStringUTF16(state.ready
                                     ? IDS_AHOI_UBO_INVENTORY_READY
                                     : IDS_AHOI_UBO_INVENTORY_NOT_READY)});
}

std::u16string InventoryText(int label_id,
                             std::string_view extension_id,
                             const UboExtensionState& state) {
  return base::StrCat({l10n_util::GetStringUTF16(label_id), u": ",
                       base::UTF8ToUTF16(extension_id), u" — ",
                       InventoryStateText(state)});
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

  views::View* inventory =
      contents->AddChildView(std::make_unique<views::View>());
  auto* inventory_layout =
      inventory->SetLayoutManager(std::make_unique<views::BoxLayout>());
  inventory_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  inventory_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  inventory_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);
  pinned_classic_inventory_ =
      inventory->AddChildView(std::make_unique<views::Label>());
  former_classic_inventory_ =
      inventory->AddChildView(std::make_unique<views::Label>());
  lite_inventory_ = inventory->AddChildView(std::make_unique<views::Label>());
  for (views::Label* label :
       {pinned_classic_inventory_.get(), former_classic_inventory_.get(),
        lite_inventory_.get()}) {
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
    if (prompt_handoff_pending_) {
      service_->ContinueInstallAfterDialogClosed();
    } else {
      service_->CancelUserInstall();
    }
  }
}

std::u16string UboInstallDialog::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_AHOI_UBO_TITLE);
}

bool UboInstallDialog::Accept() {
  switch (action_) {
    case UboDialogAction::kBeginPinnedInstall: {
      content::WebContents* prompt_host =
          browser_->tab_strip_model()->GetActiveWebContents();
      if (!prompt_host) {
        // Ahoi intentionally supports a real zero-tab window. Seed only the
        // normal Chromium prompt host in response to this explicit install
        // gesture; the extension still requires its permission confirmation.
        prompt_host = chrome::AddAndReturnTabAt(browser_, GURL(), -1, true);
      }
      service_->BeginPinnedBootstrapInstall(
          prompt_host, /*wait_for_install_dialog_close=*/true);
      return false;
    }
    case UboDialogAction::kDownloadUpdate:
      service_->PreparePackage();
      return false;
    case UboDialogAction::kInstallPreparedUpdate: {
      content::WebContents* prompt_host =
          browser_->tab_strip_model()->GetActiveWebContents();
      if (!prompt_host) {
        prompt_host = chrome::AddAndReturnTabAt(browser_, GURL(), -1, true);
      }
      service_->InstallPreparedPackage(prompt_host,
                                       /*wait_for_install_dialog_close=*/true);
      return false;
    }
    case UboDialogAction::kRemoveLite:
      service_->RequestRemoveUboLite();
      return false;
    case UboDialogAction::kClose:
      return true;
    case UboDialogAction::kNone:
      return false;
  }
}

bool UboInstallDialog::Cancel() {
  // Widget::Close() is used to detach this sheet before the browser-owned
  // permission prompt is created. Even if a platform maps that close through
  // the dialog cancel path, the accepted handoff must remain committed.
  if (!prompt_handoff_pending_) {
    service_->CancelUserInstall();
  }
  return true;
}

void UboInstallDialog::OnUboServiceStatusChanged(
    const UboServiceStatus& status) {
  Update(status);
  if (status.prompt_handoff_pending && !prompt_handoff_pending_) {
    prompt_handoff_pending_ = true;
    if (GetWidget()) {
      // The Chromium permission prompt is another window-modal surface. Close
      // this sheet first; its destructor posts the actual installer handoff
      // only after the native sheet has detached from the browser window.
      GetWidget()->Close();
    }
  }
}

void UboInstallDialog::Update(const UboServiceStatus& status) {
  UboDialogPresentation presentation = PresentUboStatus(status);
  const bool pinned_official_release =
      status.catalog && IsPinnedUboBootstrapCatalogEntry(*status.catalog);
  action_ = presentation.action;
  if (pinned_official_release &&
      status.state == UboServiceState::kCatalogReady &&
      !status.one_click_install_in_progress) {
    status_label_->SetText(
        l10n_util::GetStringUTF16(IDS_AHOI_UBO_PINNED_BOOTSTRAP_READY));
  } else {
    status_label_->SetText(
        l10n_util::GetStringUTF16(presentation.status_string_id));
  }
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
    const std::string source_label =
        base::UTF16ToUTF8(l10n_util::GetStringUTF16(
            pinned_official_release ? IDS_AHOI_UBO_OFFICIAL_GITHUB_RELEASE_LABEL
                                    : IDS_AHOI_UBO_SIGNED_CATALOG_LABEL));
    upstream_->SetText(MetadataText(
        IDS_AHOI_UBO_UPSTREAM_LABEL,
        base::StrCat({source_label, " — ", status.catalog->upstream_tag, " @ ",
                      status.catalog->upstream_commit, " — ",
                      status.catalog->upstream_source_url.spec()})));
    hash_->SetText(
        MetadataText(IDS_AHOI_UBO_HASH_LABEL, status.catalog->package_sha256));
    license_->SetText(
        MetadataText(IDS_AHOI_UBO_LICENSE_LABEL, status.catalog->license));
  }
  pinned_classic_inventory_->SetText(
      InventoryText(IDS_AHOI_UBO_INVENTORY_PINNED_CLASSIC,
                    kUboClassicExtensionId, status.inventory.classic));
  former_classic_inventory_->SetText(
      InventoryText(IDS_AHOI_UBO_INVENTORY_FORMER_CLASSIC,
                    kUboFormerClassicWebStoreExtensionId,
                    status.inventory.former_classic_web_store));
  lite_inventory_->SetText(InventoryText(
      IDS_AHOI_UBO_INVENTORY_LITE, kUboLiteExtensionId, status.inventory.lite));
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
  }
}

void ShowUboInstallDialog(Browser* browser) {
  if (!IsUboClassicEnabled() || !browser || !browser->GetProfile() ||
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
