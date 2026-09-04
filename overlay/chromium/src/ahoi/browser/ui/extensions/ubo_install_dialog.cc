// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_dialog.h"

#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "ahoi/browser/extensions/ubo_service_factory.h"
#include "ahoi/browser/ui/extensions/ubo_ui_tokens.h"
#include "base/check.h"
#include "base/functional/bind.h"
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
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ahoi::extensions {

namespace {

// `DialogDelegate` is deliberately kept separate from its contents view. The
// widget owns the view hierarchy, and this contents view owns the delegate
// until that hierarchy is torn down. This follows the client-ownership
// direction of Views without extending WidgetDelegate's deprecated ownership
// pass-key list.
class UboDialogContents final : public views::View {
 public:
  UboDialogContents() = default;
  ~UboDialogContents() override = default;

  void SetDialogOwner(std::unique_ptr<UboInstallDialog> dialog) {
    CHECK(dialog);
    CHECK(!dialog_owner_);
    dialog_owner_ = std::move(dialog);
  }

 private:
  std::unique_ptr<UboInstallDialog> dialog_owner_;
};

std::unique_ptr<views::Label> MakeBodyLabel(int string_id) {
  auto label =
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(string_id),
                                     views::style::CONTEXT_DIALOG_BODY_TEXT);
  label->SetSubpixelRenderingEnabled(false);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::Label> MakeMetadataLabel(std::u16string text = {}) {
  auto label = std::make_unique<views::Label>(
      text, views::style::CONTEXT_DIALOG_BODY_TEXT);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(true);
  label->SetAllowCharacterBreak(true);
  label->SetSelectable(true);
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

std::u16string InventoryText(int label_id, const UboExtensionState& state) {
  return base::StrCat(
      {l10n_util::GetStringUTF16(label_id), u": ", InventoryStateText(state)});
}

}  // namespace

UboInstallDialog::UboInstallDialog(Browser* browser, UboService* service)
    : browser_(browser), service_(service) {
  CHECK(browser_);
  CHECK(service_);
  RegisterWindowClosingCallback(base::BindOnce(
      &UboInstallDialog::HandleDialogClosed, base::Unretained(this)));
  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));
  set_fixed_width(ui_tokens::kDialogWidth);
  set_margins(views::LayoutProvider::Get()->GetInsetsMetric(
      views::InsetsMetric::INSETS_DIALOG));

  views::View* contents =
      SetContentsView(std::make_unique<UboDialogContents>());
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

  summary_ = contents->AddChildView(std::make_unique<views::View>());
  auto* summary_layout =
      summary_->SetLayoutManager(std::make_unique<views::BoxLayout>());
  summary_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  summary_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  summary_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);
  version_ = summary_->AddChildView(MakeMetadataLabel());
  source_ = summary_->AddChildView(MakeMetadataLabel());

  views::View* inventory =
      contents->AddChildView(std::make_unique<views::View>());
  auto* inventory_layout =
      inventory->SetLayoutManager(std::make_unique<views::BoxLayout>());
  inventory_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  inventory_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  inventory_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);
  pinned_classic_inventory_ = inventory->AddChildView(MakeMetadataLabel());
  former_classic_inventory_ = inventory->AddChildView(MakeMetadataLabel());
  lite_inventory_ = inventory->AddChildView(MakeMetadataLabel());

  contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_SECURITY_NOTICE));
  contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_CONFIRM_NOTICE));

  details_button_ = contents->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&UboInstallDialog::ToggleDetails,
                          weak_ptr_factory_.GetWeakPtr()),
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_SHOW_DETAILS)));
  details_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  details_button_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  details_button_->GetViewAccessibility().SetIsCollapsed();

  details_ = contents->AddChildView(std::make_unique<views::ScrollView>());
  details_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  details_->ClipHeightTo(
      0, views::LayoutProvider::Get()->GetDistanceMetric(
             views::DISTANCE_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT));
  auto details_contents = std::make_unique<views::View>();
  auto* details_layout =
      details_contents->SetLayoutManager(std::make_unique<views::BoxLayout>());
  details_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  details_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  details_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);

  metadata_ = details_contents->AddChildView(std::make_unique<views::View>());
  auto* metadata_layout =
      metadata_->SetLayoutManager(std::make_unique<views::BoxLayout>());
  metadata_layout->SetOrientation(views::BoxLayout::Orientation::kVertical);
  metadata_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  metadata_layout->set_between_child_spacing(ui_tokens::kRelatedSpacing);
  extension_id_ = metadata_->AddChildView(MakeMetadataLabel());
  upstream_ = metadata_->AddChildView(MakeMetadataLabel());
  hash_ = metadata_->AddChildView(MakeMetadataLabel());
  license_ = metadata_->AddChildView(MakeMetadataLabel());
  details_contents->AddChildView(MakeMetadataLabel(MetadataText(
      IDS_AHOI_UBO_INVENTORY_PINNED_CLASSIC, kUboClassicExtensionId)));
  details_contents->AddChildView(
      MakeMetadataLabel(MetadataText(IDS_AHOI_UBO_INVENTORY_FORMER_CLASSIC,
                                     kUboFormerClassicWebStoreExtensionId)));
  details_contents->AddChildView(MakeMetadataLabel(
      MetadataText(IDS_AHOI_UBO_INVENTORY_LITE, kUboLiteExtensionId)));
  details_contents->AddChildView(MakeBodyLabel(IDS_AHOI_UBO_GPL_NOTICE));
  details_->SetContents(std::move(details_contents));
  details_->SetVisible(false);

  service_->AddObserver(this);
  Update(service_->status());
}

// static
views::Widget* UboInstallDialog::CreateWidget(
    Browser* browser,
    UboService* service,
    UboInstallDialog** dialog_for_testing) {
  CHECK(browser);
  CHECK(browser->GetWindow());
  CHECK(service);
  auto dialog =
      std::unique_ptr<UboInstallDialog>(new UboInstallDialog(browser, service));
  UboInstallDialog* dialog_ptr = dialog.get();
  auto* contents =
      static_cast<UboDialogContents*>(dialog_ptr->GetContentsView());
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      dialog_ptr, browser->GetWindow()->GetNativeWindow());
  contents->SetDialogOwner(std::move(dialog));
  if (dialog_for_testing) {
    *dialog_for_testing = dialog_ptr;
  }
  return widget;
}

UboInstallDialog::~UboInstallDialog() {
  HandleDialogClosed();
}

void UboInstallDialog::HandleDialogClosed() {
  if (service_) {
    UboService* service = service_.get();
    service_ = nullptr;
    service->RemoveObserver(this);
    if (prompt_handoff_pending_) {
      service->ContinueInstallAfterDialogClosed();
    } else {
      service->CancelUserInstall();
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
      // this sheet first. HandleDialogClosed() runs only once Chromium has
      // received the native window-closing notification; the service then
      // posts the actual installer handoff to the next UI task.
      GetWidget()->Close();
    }
  }
}

void UboInstallDialog::ToggleDetails() {
  const bool expanded = !details_->GetVisible();
  details_->SetVisible(expanded);
  details_button_->SetText(l10n_util::GetStringUTF16(
      expanded ? IDS_EXTENSIONS_HIDE_DETAILS : IDS_EXTENSIONS_SHOW_DETAILS));
  if (expanded) {
    details_button_->GetViewAccessibility().SetIsExpanded();
  } else {
    details_button_->GetViewAccessibility().SetIsCollapsed();
  }
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
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
  summary_->SetVisible(presentation.show_metadata);
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
    source_->SetText(MetadataText(IDS_AHOI_UBO_UPSTREAM_LABEL, source_label));
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
  pinned_classic_inventory_->SetText(InventoryText(
      IDS_AHOI_UBO_INVENTORY_PINNED_CLASSIC, status.inventory.classic));
  former_classic_inventory_->SetText(
      InventoryText(IDS_AHOI_UBO_INVENTORY_FORMER_CLASSIC,
                    status.inventory.former_classic_web_store));
  lite_inventory_->SetText(
      InventoryText(IDS_AHOI_UBO_INVENTORY_LITE, status.inventory.lite));
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
  UboInstallDialog::CreateWidget(browser, service)->Show();
}

}  // namespace ahoi::extensions
