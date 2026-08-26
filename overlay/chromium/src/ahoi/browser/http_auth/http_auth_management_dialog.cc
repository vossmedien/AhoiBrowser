// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_management_dialog.h"

#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_management_model.h"
#include "ahoi/browser/http_auth/http_auth_secret_access_controller.h"
#include "ahoi/browser/http_auth/http_auth_secret_util.h"
#include "ahoi/browser/http_auth/http_auth_session_controller.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#endif
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/device_reauth/device_authenticator.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_auth.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

constexpr int kDialogWidth = 640;
constexpr int kMinimumListHeight = 120;
constexpr int kMaximumListHeight = 440;

std::unique_ptr<views::Label> MakeLabel(
    std::u16string text,
    int style = views::style::STYLE_BODY_4) {
  auto label = std::make_unique<views::Label>(
      std::move(text), views::style::CONTEXT_DIALOG_BODY_TEXT, style);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(true);
  return label;
}

std::unique_ptr<views::MdTextButton> MakeButton(
    views::Button::PressedCallback callback,
    int string_id) {
  auto button = std::make_unique<views::MdTextButton>(
      std::move(callback), l10n_util::GetStringUTF16(string_id));
  button->SetTextSubpixelRenderingEnabled(false);
  return button;
}

std::u16string OriginAndRealm(const HttpAuthProtectionSpace& space) {
  const std::string origin = base::StrCat(
      {space.origin.scheme(), "://",
       net::HostPortPair(space.origin.host(), space.origin.port()).ToString()});
  return l10n_util::GetStringFUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_REALM_HEADER,
                                    base::UTF8ToUTF16(origin),
                                    base::UTF8ToUTF16(space.realm));
}

std::u16string SchemeName(net::HttpAuth::Scheme scheme) {
  switch (scheme) {
    case net::HttpAuth::AUTH_SCHEME_BASIC:
      return u"Basic";
    case net::HttpAuth::AUTH_SCHEME_DIGEST:
      return u"Digest";
    default:
      return std::u16string();
  }
}

std::u16string TargetName(net::HttpAuth::Target target) {
  return l10n_util::GetStringUTF16(
      target == net::HttpAuth::AUTH_PROXY
          ? IDS_AHOI_HTTP_AUTH_MANAGER_TARGET_PROXY
          : IDS_AHOI_HTTP_AUTH_MANAGER_TARGET_SERVER);
}

std::u16string AccountDetails(const HttpAuthCredentialMetadata& metadata) {
  const std::u16string last_used =
      metadata.last_successful.is_null()
          ? l10n_util::GetStringUTF16(
                IDS_AHOI_HTTP_AUTH_MANAGER_LAST_USED_NEVER)
          : base::TimeFormatShortDateAndTime(metadata.last_successful);
  return l10n_util::GetStringFUTF16(
      IDS_AHOI_HTTP_AUTH_MANAGER_ACCOUNT_DETAILS,
      SchemeName(metadata.protection_space.scheme),
      TargetName(metadata.protection_space.target), last_used);
}

bool SameGroup(const HttpAuthCredentialMetadata& lhs,
               const HttpAuthCredentialMetadata& rhs) {
  return IsSameManagedHttpAuthRealm(lhs.protection_space, rhs.protection_space);
}

std::unique_ptr<device_reauth::DeviceAuthenticator> CreateSystemAuthenticator(
    Profile* profile) {
#if BUILDFLAG(IS_MAC)
  if (!profile || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return ChromeDeviceAuthenticatorFactory::GetForProfile(
      profile,
      device_reauth::DeviceAuthParams(
          base::Seconds(0), device_reauth::DeviceAuthSource::kPasswordManager));
#else
  (void)profile;
  return nullptr;
#endif
}

}  // namespace

HttpAuthManagementDialog::HttpAuthManagementDialog(
    content::WebContents* source_web_contents,
    HttpAuthCredentialService* service)
    : content::WebContentsObserver(source_web_contents), service_(service) {
  CHECK(source_web_contents);
  CHECK(service_);
  Profile* profile =
      Profile::FromBrowserContext(source_web_contents->GetBrowserContext());
  CHECK(profile);
  CHECK(profile->IsRegularProfile());
  secret_access_controller_ = std::make_unique<HttpAuthSecretAccessController>(
      CreateSystemAuthenticator(profile),
      base::BindRepeating(&HttpAuthManagementDialog::IsSecretContextValid,
                          base::Unretained(this)),
      base::BindRepeating(&HttpAuthManagementDialog::LoadSavedSecret,
                          base::Unretained(this)));

  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  set_fixed_width(kDialogWidth);
  set_margins(views::LayoutProvider::Get()->GetInsetsMetric(
      views::InsetsMetric::INSETS_DIALOG));

  auto contents = std::make_unique<views::View>();
  contents->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  contents->SetAccessibleName(GetWindowTitle());
  auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto security = MakeLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_SECURITY_NOTICE));
  contents->AddChildView(std::move(security));

  auto search = std::make_unique<views::Textfield>();
  search->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_SEARCH_PLACEHOLDER));
  search->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_SEARCH_PLACEHOLDER));
  search_field_ = contents->AddChildView(std::move(search));
  search_subscription_ =
      search_field_->AddTextChangedCallback(base::BindRepeating(
          &HttpAuthManagementDialog::OnSearchChanged, base::Unretained(this)));

  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackgroundColor(std::nullopt);
  scroll->ClipHeightTo(kMinimumListHeight, kMaximumListHeight);
  auto rows = std::make_unique<views::View>();
  auto* rows_layout = rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  rows_container_ = rows.get();
  scroll->SetContents(std::move(rows));
  scroll_view_ = contents->AddChildView(std::move(scroll));

  auto editor = std::make_unique<views::View>();
  auto* editor_layout =
      editor->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 8), 6));
  editor_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  editor_heading_ = editor->AddChildView(
      MakeLabel(std::u16string(), views::style::STYLE_HEADLINE_5));
  editor->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_EDITOR_USERNAME)));
  auto editor_username = std::make_unique<views::Textfield>();
  editor_username->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_EDITOR_USERNAME));
  editor_username_field_ = editor->AddChildView(std::move(editor_username));
  editor->AddChildView(MakeLabel(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_EDITOR_PASSWORD)));
  auto editor_password = std::make_unique<views::Textfield>();
  editor_password->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_EDITOR_PASSWORD));
  editor_password->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  editor_password_field_ = editor->AddChildView(std::move(editor_password));

  auto editor_actions = std::make_unique<views::View>();
  auto* editor_actions_layout =
      editor_actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  editor_actions_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);
  auto reveal_button = MakeButton(
      base::BindRepeating(&HttpAuthManagementDialog::RevealOrHidePassword,
                          weak_ptr_factory_.GetWeakPtr()),
      IDS_AHOI_HTTP_AUTH_MANAGER_SHOW_PASSWORD);
  reveal_password_button_ = reveal_button.get();
  editor_actions->AddChildView(std::move(reveal_button));
  editor_actions->AddChildView(
      MakeButton(base::BindRepeating(&HttpAuthManagementDialog::CopyPassword,
                                     weak_ptr_factory_.GetWeakPtr()),
                 IDS_AHOI_HTTP_AUTH_MANAGER_COPY_PASSWORD));
  editor_actions->AddChildView(MakeButton(
      base::BindRepeating(&HttpAuthManagementDialog::SaveEditedCredential,
                          weak_ptr_factory_.GetWeakPtr()),
      IDS_AHOI_HTTP_AUTH_MANAGER_SAVE_CHANGES));
  editor_actions->AddChildView(
      MakeButton(base::BindRepeating(&HttpAuthManagementDialog::CancelEdit,
                                     weak_ptr_factory_.GetWeakPtr()),
                 IDS_AHOI_HTTP_AUTH_MANAGER_CANCEL_EDIT));
  editor->AddChildView(std::move(editor_actions));
  editor->SetVisible(false);
  editor_container_ = contents->AddChildView(std::move(editor));

  empty_label_ = contents->AddChildView(MakeLabel(std::u16string()));
  status_label_ = contents->AddChildView(MakeLabel(std::u16string()));
  status_label_->GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  status_label_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite,
      views::ViewAccessibility::kLiveRegionRelevantDefault,
      /*atomic=*/true);
  status_label_->SetVisible(false);

  SetContentsView(std::move(contents));
  RebuildRows();
}

void HttpAuthManagementDialog::RebuildRows() {
  pending_confirmation_button_ = nullptr;
  rows_container_->RemoveAllChildViews();
  visible_credential_count_ = 0;
  const std::u16string query(search_field_->GetText());
  const GURL source_url =
      web_contents() ? web_contents()->GetLastCommittedURL() : GURL();
  std::vector<HttpAuthManagementEntry> entries =
      BuildHttpAuthManagementEntries(service_->GetMetadataSnapshot(), query,
                                     source_url, ActiveProtectionSpace());
  visible_credential_count_ = entries.size();

  const HttpAuthCredentialMetadata* previous = nullptr;
  for (const HttpAuthManagementEntry& entry : entries) {
    if (!previous || !SameGroup(*previous, entry.metadata)) {
      AddRealmHeader(entry.metadata);
    }
    AddCredentialRow(entry);
    previous = &entry.metadata;
  }

  size_t never_save_count = 0;
  for (const HttpAuthProtectionSpace& protection_space :
       service_->GetNeverSaveSnapshot()) {
    if (!protection_space.IsValid() ||
        !HttpAuthProtectionSpaceMatchesManagementQuery(protection_space,
                                                       query)) {
      continue;
    }
    AddNeverSaveRow(protection_space);
    ++never_save_count;
  }

  const bool empty = entries.empty() && never_save_count == 0;
  empty_label_->SetText(l10n_util::GetStringUTF16(
      query.empty() ? IDS_AHOI_HTTP_AUTH_MANAGER_EMPTY
                    : IDS_AHOI_HTTP_AUTH_MANAGER_NO_RESULTS));
  empty_label_->SetVisible(empty);
  scroll_view_->SetVisible(!empty);
  if (pending_confirmation_button_) {
    pending_confirmation_button_->RequestFocus();
  }
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
  }
}

void HttpAuthManagementDialog::AddRealmHeader(
    const HttpAuthCredentialMetadata& metadata) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto label = MakeLabel(OriginAndRealm(metadata.protection_space),
                         views::style::STYLE_HEADLINE_5);
  views::View* label_ptr = row->AddChildView(std::move(label));
  layout->SetFlexForView(label_ptr, 1);
  const bool armed = IsDeletionArmed(metadata, /*entire_realm=*/true);
  auto delete_button =
      MakeButton(base::BindRepeating(&HttpAuthManagementDialog::DeleteRealm,
                                     weak_ptr_factory_.GetWeakPtr(), metadata),
                 armed ? IDS_AHOI_HTTP_AUTH_MANAGER_CONFIRM_DELETE_REALM
                       : IDS_AHOI_HTTP_AUTH_MANAGER_DELETE_REALM);
  if (armed) {
    pending_confirmation_button_ = delete_button.get();
  }
  row->AddChildView(std::move(delete_button));
  rows_container_->AddChildView(std::move(row));
}

void HttpAuthManagementDialog::AddCredentialRow(
    const HttpAuthManagementEntry& entry) {
  auto card = std::make_unique<views::View>();
  auto* card_layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 8), 5));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  const std::u16string account_name =
      entry.metadata.preferred
          ? l10n_util::GetStringFUTF16(
                IDS_AHOI_HTTP_AUTH_MANAGER_PREFERRED_ACCOUNT,
                entry.metadata.username)
          : entry.metadata.username;
  card->AddChildView(
      MakeLabel(account_name, views::style::STYLE_BODY_3_MEDIUM));
  card->AddChildView(MakeLabel(AccountDetails(entry.metadata)));

  auto actions = std::make_unique<views::View>();
  auto* actions_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  actions_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);
  if (!entry.metadata.preferred) {
    actions->AddChildView(MakeButton(
        base::BindRepeating(&HttpAuthManagementDialog::MakePreferred,
                            weak_ptr_factory_.GetWeakPtr(), entry.metadata),
        IDS_AHOI_HTTP_AUTH_MANAGER_MAKE_PREFERRED));
  }
  if (entry.can_switch_account) {
    actions->AddChildView(MakeButton(
        base::BindRepeating(&HttpAuthManagementDialog::SwitchAccount,
                            weak_ptr_factory_.GetWeakPtr(), entry.metadata),
        IDS_AHOI_HTTP_AUTH_MANAGER_SWITCH_ACCOUNT));
  }
  actions->AddChildView(MakeButton(
      base::BindRepeating(&HttpAuthManagementDialog::BeginEdit,
                          weak_ptr_factory_.GetWeakPtr(), entry.metadata),
      IDS_AHOI_HTTP_AUTH_MANAGER_EDIT_ACCOUNT));
  const bool armed = IsDeletionArmed(entry.metadata, /*entire_realm=*/false);
  auto delete_button = MakeButton(
      base::BindRepeating(&HttpAuthManagementDialog::DeleteCredential,
                          weak_ptr_factory_.GetWeakPtr(), entry.metadata),
      armed ? IDS_AHOI_HTTP_AUTH_MANAGER_CONFIRM_DELETE_ACCOUNT
            : IDS_AHOI_HTTP_AUTH_MANAGER_DELETE_ACCOUNT);
  if (armed) {
    pending_confirmation_button_ = delete_button.get();
  }
  actions->AddChildView(std::move(delete_button));
  card->AddChildView(std::move(actions));
  rows_container_->AddChildView(std::move(card));
  rows_container_->AddChildView(std::make_unique<views::Separator>());
}

void HttpAuthManagementDialog::AddNeverSaveRow(
    const HttpAuthProtectionSpace& protection_space) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 0), 8));
  auto label = MakeLabel(
      l10n_util::GetStringFUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_NEVER_SAVE_REALM,
                                 OriginAndRealm(protection_space)));
  views::View* label_ptr = row->AddChildView(std::move(label));
  layout->SetFlexForView(label_ptr, 1);
  row->AddChildView(MakeButton(
      base::BindRepeating(&HttpAuthManagementDialog::ResetNeverSave,
                          weak_ptr_factory_.GetWeakPtr(), protection_space),
      IDS_AHOI_HTTP_AUTH_MANAGER_ALLOW_SAVING));
  rows_container_->AddChildView(std::move(row));
}

void HttpAuthManagementDialog::BeginEdit(HttpAuthCredentialMetadata metadata) {
  ClearEditor();
  pending_deletion_.reset();
  if (!secret_access_controller_ || !IsCredentialStillManaged(metadata)) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  secret_access_controller_->RequestSecret(
      metadata,
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_PROMPT),
      base::BindOnce(&HttpAuthManagementDialog::OnEditSecretLoaded,
                     weak_ptr_factory_.GetWeakPtr(), std::move(metadata)));
}

void HttpAuthManagementDialog::OnEditSecretLoaded(
    HttpAuthCredentialMetadata metadata,
    std::optional<std::u16string> secret) {
  if (!secret || secret->empty() || !IsSecretContextValid() ||
      !IsCredentialStillManaged(metadata)) {
    if (secret) {
      SecurelyClearHttpAuthSecret(&*secret);
    }
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_FAILED);
    return;
  }

  editing_metadata_ = metadata;
  editor_heading_->SetText(l10n_util::GetStringFUTF16(
      IDS_AHOI_HTTP_AUTH_MANAGER_EDITOR_HEADING, metadata.username));
  editor_username_field_->SetText(metadata.username);
  editor_password_field_->SetText(*secret);
  SecurelyClearHttpAuthSecret(&*secret);
  MaskEditorPassword();
  editor_container_->SetVisible(true);
  status_label_->SetVisible(false);
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
  }
  editor_username_field_->RequestFocus();
}

void HttpAuthManagementDialog::RevealOrHidePassword() {
  if (!editing_metadata_) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  if (password_revealed_) {
    MaskEditorPassword();
    return;
  }
  if (!secret_access_controller_) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_FAILED);
    return;
  }
  secret_access_controller_->Authorize(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_PROMPT),
      base::BindOnce(&HttpAuthManagementDialog::OnRevealAuthorized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void HttpAuthManagementDialog::OnRevealAuthorized(bool authenticated) {
  if (!authenticated) {
    MaskEditorPassword();
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_FAILED);
    return;
  }
  if (!editing_metadata_ || !IsSecretContextValid() ||
      !IsCredentialStillManaged(*editing_metadata_)) {
    ClearEditor();
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  password_revealed_ = true;
  editor_password_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_TEXT);
  reveal_password_button_->SetText(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_HIDE_PASSWORD));
}

void HttpAuthManagementDialog::CopyPassword() {
  if (!editing_metadata_ || !secret_access_controller_) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  secret_access_controller_->RequestSecret(
      *editing_metadata_,
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_PROMPT),
      base::BindOnce(&HttpAuthManagementDialog::OnCopySecretLoaded,
                     weak_ptr_factory_.GetWeakPtr()));
}

void HttpAuthManagementDialog::OnCopySecretLoaded(
    std::optional<std::u16string> secret) {
  if (!secret || secret->empty() || !editing_metadata_ ||
      !IsSecretContextValid()) {
    if (secret) {
      SecurelyClearHttpAuthSecret(&*secret);
    }
    if (editing_metadata_ && !IsCredentialStillManaged(*editing_metadata_)) {
      ClearEditor();
      ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    } else {
      ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_FAILED);
    }
    return;
  }
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(*secret);
    writer.MarkAsConfidential();
  }
  copied_secret_sequence_ =
      ui::Clipboard::GetForCurrentThread()->GetSequenceNumber(
          ui::ClipboardBuffer::kCopyPaste);
  SecurelyClearHttpAuthSecret(&*secret);
  ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_PASSWORD_COPIED);
}

void HttpAuthManagementDialog::SaveEditedCredential() {
  if (!editing_metadata_ || editor_username_field_->GetText().empty() ||
      editor_password_field_->GetText().empty() || !secret_access_controller_) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  MaskEditorPassword();
  secret_access_controller_->Authorize(
      l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_PROMPT),
      base::BindOnce(&HttpAuthManagementDialog::OnSaveAuthorized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void HttpAuthManagementDialog::OnSaveAuthorized(bool authenticated) {
  if (!authenticated) {
    MaskEditorPassword();
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_AUTH_FAILED);
    return;
  }
  if (!editing_metadata_ || !IsSecretContextValid() ||
      !IsCredentialStillManaged(*editing_metadata_) ||
      editor_username_field_->GetText().empty() ||
      editor_password_field_->GetText().empty()) {
    ClearEditor();
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }

  HttpAuthCredentialMetadata metadata = *editing_metadata_;
  std::u16string new_username(editor_username_field_->GetText());
  std::u16string new_password(editor_password_field_->GetText());
  ClearEditor();
  service_->UpdateCredential(
      metadata.protection_space, metadata.username, std::move(new_username),
      std::move(new_password), HttpAuthRequestContext::kRegular,
      base::BindOnce(&HttpAuthManagementDialog::OnCredentialUpdateFinished,
                     weak_ptr_factory_.GetWeakPtr()));
  SecurelyClearHttpAuthSecret(&new_password);
}

void HttpAuthManagementDialog::ClearEditor() {
  if (secret_access_controller_) {
    secret_access_controller_->Invalidate();
  }
  MaskEditorPassword();
  if (editor_password_field_) {
    std::u16string secret(editor_password_field_->GetText());
    if (!secret.empty()) {
      std::u16string zeroes(secret.size(), u'\0');
      editor_password_field_->SetText(zeroes);
      SecurelyClearHttpAuthSecret(&zeroes);
    }
    editor_password_field_->SetText(std::u16string_view());
    SecurelyClearHttpAuthSecret(&secret);
  }
  if (editor_username_field_) {
    editor_username_field_->SetText(std::u16string_view());
  }
  if (editor_heading_) {
    editor_heading_->SetText(std::u16string_view());
  }
  if (editor_container_) {
    editor_container_->SetVisible(false);
  }
  editing_metadata_.reset();
  ClearCopiedSecretIfUnchanged();
}

void HttpAuthManagementDialog::ClearCopiedSecretIfUnchanged() {
  if (!copied_secret_sequence_) {
    return;
  }
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  if (clipboard->GetSequenceNumber(ui::ClipboardBuffer::kCopyPaste) ==
      *copied_secret_sequence_) {
    clipboard->Clear(ui::ClipboardBuffer::kCopyPaste);
  }
  copied_secret_sequence_.reset();
}

void HttpAuthManagementDialog::MaskEditorPassword() {
  password_revealed_ = false;
  if (editor_password_field_) {
    editor_password_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  }
  if (reveal_password_button_) {
    reveal_password_button_->SetText(
        l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_SHOW_PASSWORD));
  }
}

void HttpAuthManagementDialog::LoadSavedSecret(
    const HttpAuthCredentialMetadata& metadata,
    base::OnceCallback<void(std::optional<std::u16string>)> callback) {
  if (!callback || !IsSecretContextValid() ||
      !IsCredentialStillManaged(metadata)) {
    if (callback) {
      std::move(callback).Run(std::nullopt);
    }
    return;
  }
  const std::string request_path =
      metadata.protection_space.permitted_paths.empty()
          ? std::string("/")
          : metadata.protection_space.permitted_paths.front();
  service_->GetCredentials(
      metadata.protection_space, request_path, HttpAuthRequestContext::kRegular,
      HttpAuthSelectionMode::kExplicitUserSelection,
      base::BindOnce(&HttpAuthManagementDialog::OnSavedSecretLookupComplete,
                     weak_ptr_factory_.GetWeakPtr(), metadata,
                     std::move(callback)));
}

void HttpAuthManagementDialog::OnSavedSecretLookupComplete(
    HttpAuthCredentialMetadata metadata,
    base::OnceCallback<void(std::optional<std::u16string>)> callback,
    std::vector<HttpAuthCredential> credentials) {
  std::optional<std::u16string> selected;
  if (IsSecretContextValid() && IsCredentialStillManaged(metadata)) {
    for (HttpAuthCredential& credential : credentials) {
      if (!selected &&
          IsSameManagedHttpAuthCredential(credential.metadata, metadata)) {
        selected = std::move(credential.password);
      }
    }
  }
  for (HttpAuthCredential& credential : credentials) {
    SecurelyClearHttpAuthSecret(&credential.password);
  }
  std::move(callback).Run(std::move(selected));
}

bool HttpAuthManagementDialog::IsSecretContextValid() const {
  if (!web_contents() || !web_contents()->GetTopLevelNativeWindow()) {
    return false;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  return profile && profile->IsRegularProfile() &&
         !web_contents()->GetBrowserContext()->IsOffTheRecord();
}

bool HttpAuthManagementDialog::IsCredentialStillManaged(
    const HttpAuthCredentialMetadata& metadata) const {
  return std::ranges::any_of(
      service_->GetMetadataSnapshot(),
      [&metadata](const HttpAuthCredentialMetadata& candidate) {
        return IsSameManagedHttpAuthCredential(candidate, metadata);
      });
}

void HttpAuthManagementDialog::MakePreferred(
    HttpAuthCredentialMetadata metadata) {
  ClearEditor();
  pending_deletion_.reset();
  if (!service_->SetPreferredCredential(metadata.protection_space,
                                        metadata.username,
                                        HttpAuthRequestContext::kRegular)) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_PREFERRED_UPDATED);
  ScheduleRebuildRows();
}

void HttpAuthManagementDialog::SwitchAccount(
    HttpAuthCredentialMetadata metadata) {
  ClearEditor();
  pending_deletion_.reset();
  if (!web_contents() ||
      web_contents()->GetBrowserContext()->IsOffTheRecord() ||
      !CanSwitchManagedHttpAuthAccount(metadata,
                                       web_contents()->GetLastCommittedURL(),
                                       ActiveProtectionSpace()) ||
      !service_->SetPreferredCredential(metadata.protection_space,
                                        metadata.username,
                                        HttpAuthRequestContext::kRegular)) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  HttpAuthSessionController* controller =
      HttpAuthSessionController::GetOrCreate(web_contents());
  if (!controller) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  controller->SwitchAccount();
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void HttpAuthManagementDialog::DeleteCredential(
    HttpAuthCredentialMetadata metadata) {
  ClearEditor();
  if (!IsDeletionArmed(metadata, /*entire_realm=*/false)) {
    pending_deletion_ = PendingDeletion{metadata, false};
    ScheduleRebuildRows();
    return;
  }
  pending_deletion_.reset();
  service_->DeleteCredential(
      metadata.protection_space, metadata.username,
      HttpAuthRequestContext::kRegular,
      base::BindOnce(&HttpAuthManagementDialog::OnCredentialDeletionFinished,
                     weak_ptr_factory_.GetWeakPtr(), metadata));
}

void HttpAuthManagementDialog::DeleteRealm(
    HttpAuthCredentialMetadata metadata) {
  ClearEditor();
  if (!IsDeletionArmed(metadata, /*entire_realm=*/true)) {
    pending_deletion_ = PendingDeletion{metadata, true};
    ScheduleRebuildRows();
    return;
  }
  pending_deletion_.reset();
  service_->DeleteRealm(
      metadata.protection_space, HttpAuthRequestContext::kRegular,
      base::BindOnce(&HttpAuthManagementDialog::OnRealmDeletionFinished,
                     weak_ptr_factory_.GetWeakPtr(),
                     metadata.protection_space));
}

void HttpAuthManagementDialog::ResetNeverSave(
    HttpAuthProtectionSpace protection_space) {
  ClearEditor();
  pending_deletion_.reset();
  if (!service_->SetNeverSaveForRealm(protection_space, false,
                                      HttpAuthRequestContext::kRegular)) {
    ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
    return;
  }
  ShowStatus(IDS_AHOI_HTTP_AUTH_MANAGER_SAVING_ALLOWED);
  ScheduleRebuildRows();
}

void HttpAuthManagementDialog::OnCredentialDeletionFinished(
    HttpAuthCredentialMetadata metadata) {
  const bool still_present = std::ranges::any_of(
      service_->GetMetadataSnapshot(),
      [&metadata](const HttpAuthCredentialMetadata& candidate) {
        return IsSameManagedHttpAuthCredential(candidate, metadata);
      });
  ShowStatus(still_present ? IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED
                           : IDS_AHOI_HTTP_AUTH_MANAGER_ACCOUNT_DELETED);
  ScheduleRebuildRows();
}

void HttpAuthManagementDialog::OnRealmDeletionFinished(
    HttpAuthProtectionSpace protection_space) {
  const bool still_present = std::ranges::any_of(
      service_->GetMetadataSnapshot(),
      [&protection_space](const HttpAuthCredentialMetadata& candidate) {
        return IsSameManagedHttpAuthRealm(candidate.protection_space,
                                          protection_space);
      });
  ShowStatus(still_present ? IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED
                           : IDS_AHOI_HTTP_AUTH_MANAGER_REALM_DELETED);
  ScheduleRebuildRows();
}

}  // namespace ahoi
