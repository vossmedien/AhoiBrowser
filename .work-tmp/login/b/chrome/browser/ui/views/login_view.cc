// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/login_view.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_saved_account_menu.h"
#include "ahoi/browser/ui/dialog_style.h"
#include "ahoi/browser/ui/visual_style.h"
#include "base/functional/bind.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/range/range.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/editable_combobox/editable_combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/table_layout.h"
#include "ui/views/layout/table_layout_view.h"
#include "ui/views/window/dialog_delegate.h"

namespace {

// Radio group of the saved-account rows; the persistence choice is 1.
constexpr int kSavedAccountGroup = 2;
// The dialog frame supplies the 24 padding, so the text column is the
// dialog width without it.
constexpr int kMessageWidth = ahoi::visual_style::kAuthDialogWidth -
                              2 * ahoi::visual_style::kDialogPadding;

void AddSectionSeparator(views::View& parent) {
  parent.AddChildView(std::make_unique<views::Separator>())
      ->SetColorId(ahoi::visual_style::kDivider);
}

}  // namespace

LoginView::LoginView(const std::u16string& authority,
                     const std::u16string& explanation,
                     LoginHandler::LoginModelData* login_model_data)
    : login_handler_(login_model_data
                         ? login_model_data->login_handler->GetWeakPtr()
                         : base::WeakPtr<LoginHandler>()),
      realm_for_display_(login_handler_
                             ? login_handler_->GetAuthRealmForDisplay()
                             : std::u16string()),
      http_auth_manager_(
          login_model_data && login_model_data->model && login_handler_ &&
                  login_handler_->CanUseGenericCredentialManagement()
              ? login_model_data->model.get()
              : nullptr) {
  ChromeLayoutProvider* provider = ChromeLayoutProvider::Get();
  // ApplyAhoiDialogFrame() gives the dialog its 24 padding; sections are
  // 16 apart and divided by hairlines.
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      ahoi::visual_style::kDialogSectionSpacing));

  auto* authority_container =
      AddChildView(std::make_unique<views::BoxLayoutView>());
  authority_container->SetOrientation(views::BoxLayout::Orientation::kVertical);
  auto* authority_label =
      authority_container->AddChildView(std::make_unique<views::Label>(
          authority, views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
  authority_label->SetMultiLine(true);
  authority_label->SetMaximumWidth(kMessageWidth);
  authority_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  authority_label->SetAllowCharacterBreak(true);
  if (!explanation.empty()) {
    auto* explanation_label = authority_container->AddChildView(
        std::make_unique<views::Label>(explanation, views::style::CONTEXT_LABEL,
                                       views::style::STYLE_SECONDARY));
    explanation_label->SetMultiLine(true);
    explanation_label->SetMaximumWidth(kMessageWidth);
    explanation_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  if (login_handler_ && login_handler_->HasAhoiCredentialService()) {
    auto* realm_label =
        authority_container->AddChildView(std::make_unique<views::Label>(
            l10n_util::GetStringFUTF16(
                IDS_LOGIN_DIALOG_REALM,
                login_handler_->GetAuthRealmForDisplay()),
            views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    realm_label->SetSubpixelRenderingEnabled(false);
    realm_label->SetMultiLine(true);
    realm_label->SetMaximumWidth(kMessageWidth);
    realm_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    auto* scheme_label =
        authority_container->AddChildView(std::make_unique<views::Label>(
            l10n_util::GetStringFUTF16(
                IDS_LOGIN_DIALOG_AUTH_SCHEME,
                login_handler_->GetAuthSchemeForDisplay()),
            views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    scheme_label->SetSubpixelRenderingEnabled(false);
    scheme_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    const int transport_message =
        login_handler_->IsCredentialTransportSecure()
            ? IDS_LOGIN_DIALOG_SECURE_TRANSPORT
            : IDS_LOGIN_DIALOG_INSECURE_TRANSPORT_WARNING;
    auto* transport_label =
        authority_container->AddChildView(std::make_unique<views::Label>(
            l10n_util::GetStringUTF16(transport_message),
            views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    transport_label->SetSubpixelRenderingEnabled(false);
    transport_label->SetMultiLine(true);
    transport_label->SetMaximumWidth(kMessageWidth);
    transport_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    if (login_handler_->previous_attempt_failed()) {
      auto* failure_label =
          authority_container->AddChildView(std::make_unique<views::Label>(
              l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_RETRY_FAILED),
              views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
      failure_label->SetSubpixelRenderingEnabled(false);
      failure_label->SetMultiLine(true);
      failure_label->SetMaximumWidth(kMessageWidth);
      failure_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    }
  }

  // Ahoi: the save choice follows the fields, as the user fills them first.
  views::View* persistence_choice = nullptr;
  // Subresource challenges still identify their exact origin, realm, scheme,
  // and transport above, but cannot safely participate in the main-frame
  // success-confirmation transaction. Keep every stored-account and
  // persistence action absent for those prompts instead of presenting inert
  // controls.
  if (login_handler_ && login_handler_->CanManageAhoiCredentials()) {
    auto* saved_accounts =
        AddChildView(std::make_unique<views::BoxLayoutView>());
    saved_accounts->SetOrientation(views::BoxLayout::Orientation::kVertical);
    saved_accounts->SetBetweenChildSpacing(
        ahoi::visual_style::kDialogLabelSpacing);
    saved_accounts->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    saved_accounts_container_ = saved_accounts;
    saved_accounts_container_->SetVisible(false);
    AddSectionSeparator(*saved_accounts);

    // The saved accounts as radio rows, preferred first. The username
    // field's menu still narrows by prefix while typing.
    auto* rows_heading =
        saved_accounts->AddChildView(std::make_unique<views::Label>(
            l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_SAVED_ACCOUNTS),
            views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    rows_heading->SetSubpixelRenderingEnabled(false);
    rows_heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* rows = saved_accounts->AddChildView(
        std::make_unique<views::BoxLayoutView>());
    rows->SetOrientation(views::BoxLayout::Orientation::kVertical);
    rows->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    rows->GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
    rows->GetViewAccessibility().SetName(*rows_heading);
    saved_account_rows_ = rows;

    use_saved_account_button_ =
        saved_accounts->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&LoginView::OnUseSavedAccountPressed,
                                weak_ptr_factory_.GetWeakPtr()),
            l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_USE_SAVED_ACCOUNT)));
    use_saved_account_button_->SetTextSubpixelRenderingEnabled(false);
    use_saved_account_button_->SetVisible(false);

    auto* account_actions =
        saved_accounts->AddChildView(std::make_unique<views::BoxLayoutView>());
    account_actions->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    account_actions->SetBetweenChildSpacing(
        provider->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL));
    make_preferred_button_ =
        account_actions->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&LoginView::OnMakePreferredPressed,
                                weak_ptr_factory_.GetWeakPtr()),
            l10n_util::GetStringUTF16(
                IDS_LOGIN_DIALOG_MAKE_ACCOUNT_PREFERRED)));
    make_preferred_button_->SetTextSubpixelRenderingEnabled(false);
    delete_saved_account_button_ =
        account_actions->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&LoginView::OnDeleteSavedAccountPressed,
                                weak_ptr_factory_.GetWeakPtr()),
            l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_DELETE_SAVED_ACCOUNT)));
    delete_saved_account_button_->SetTextSubpixelRenderingEnabled(false);
    make_preferred_button_->SetVisible(false);
    delete_saved_account_button_->SetVisible(false);

    if (!login_handler_->IsAhoiCredentialRequestIncognito()) {
      auto* persistence =
          AddChildView(std::make_unique<views::BoxLayoutView>());
      persistence->SetOrientation(views::BoxLayout::Orientation::kVertical);
      persistence->SetBetweenChildSpacing(provider->GetDistanceMetric(
          views::DISTANCE_RELATED_CONTROL_VERTICAL));
      constexpr int kCredentialPersistenceGroup = 1;
      auto* use_once_radio =
          persistence->AddChildView(std::make_unique<views::RadioButton>(
              l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_USE_CREDENTIAL_ONCE),
              kCredentialPersistenceGroup));
      use_once_radio->SetTextSubpixelRenderingEnabled(false);
      save_radio_ =
          persistence->AddChildView(std::make_unique<views::RadioButton>(
              l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_SAVE_CREDENTIAL),
              kCredentialPersistenceGroup));
      save_radio_->SetTextSubpixelRenderingEnabled(false);
      never_save_radio_ =
          persistence->AddChildView(std::make_unique<views::RadioButton>(
              l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_NEVER_SAVE_CREDENTIAL),
              kCredentialPersistenceGroup));
      never_save_radio_->SetTextSubpixelRenderingEnabled(false);
      use_once_radio->SetChecked(true);
      persistence_choice = persistence;
    }
  }

  AddSectionSeparator(*this);
  auto* fields_container =
      AddChildView(std::make_unique<views::TableLayoutView>());
  fields_container
      ->AddColumn(views::LayoutAlignment::kStart,
                  views::LayoutAlignment::kCenter,
                  views::TableLayout::kFixedSize,
                  views::TableLayout::ColumnSize::kUsePreferred, 0, 0)
      .AddPaddingColumn(
          views::TableLayout::kFixedSize,
          provider->GetDistanceMetric(views::DISTANCE_RELATED_LABEL_HORIZONTAL))
      .AddColumn(views::LayoutAlignment::kStretch,
                 views::LayoutAlignment::kStretch, 1.0,
                 views::TableLayout::ColumnSize::kFixed, 0, 0)
      .AddRows(1, views::TableLayout::kFixedSize)
      .AddPaddingRow(views::TableLayout::kFixedSize,
                     ahoi::visual_style::kDialogLabelSpacing)
      .AddRows(1, views::TableLayout::kFixedSize);
  auto* username_label =
      fields_container->AddChildView(std::make_unique<views::Label>(
          l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_USERNAME_FIELD),
          views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
  auto saved_accounts_model = std::make_unique<ui::SimpleComboboxModel>(
      std::vector<ui::SimpleComboboxModel::Item>());
  saved_account_model_ = saved_accounts_model.get();
  // RefreshSavedAccountModel() filters the menu itself: the combobox's own
  // prefix filter would hide every other account behind a prefilled one.
  username_field_ =
      fields_container->AddChildView(std::make_unique<views::EditableCombobox>(
          std::move(saved_accounts_model), /*filter_on_edit=*/false,
          /*show_on_empty=*/true));
  username_field_->GetViewAccessibility().SetName(*username_label);
  username_field_->SetPreferredSize(
      gfx::Size(username_field_->GetPreferredSize().width(),
                ahoi::visual_style::kDialogFieldHeight));
  ahoi::dialog_style::StyleFocusRing(username_field_->GetTextfield());
  username_field_->SetCallback(base::BindRepeating(
      &LoginView::OnUsernameChanged, weak_ptr_factory_.GetWeakPtr()));
  auto* password_label =
      fields_container->AddChildView(std::make_unique<views::Label>(
          l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_PASSWORD_FIELD),
          views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
  password_field_ =
      fields_container->AddChildView(std::make_unique<views::Textfield>());
  password_field_->GetViewAccessibility().SetName(*password_label);
  password_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  ahoi::dialog_style::StyleDialogField(*password_field_);
  if (persistence_choice) {
    ReorderChildView(persistence_choice, children().size());
  }

  if (http_auth_manager_ && login_handler_ &&
      login_handler_->CanUseGenericCredentialManagement()) {
    http_auth_manager_->SetObserverAndDeliverCredentials(
        this, *login_model_data->form);
  }

  if (login_handler_ && login_handler_->CanManageAhoiCredentials()) {
    Profile* profile =
        login_handler_->web_contents()
            ? Profile::FromBrowserContext(
                  login_handler_->web_contents()->GetBrowserContext())
            : nullptr;
    if (profile) {
      favicon::FaviconService* favicon_service =
          FaviconServiceFactory::GetForProfile(
              profile, ServiceAccessType::EXPLICIT_ACCESS);
      if (favicon_service) {
        favicon_service->GetFaviconImageForPageURL(
            login_handler_->GetCredentialOriginUrl(),
            base::BindOnce(&LoginView::OnFaviconAvailable,
                           weak_ptr_factory_.GetWeakPtr()),
            &favicon_task_tracker_);
      }
    }

    if (login_handler_->IsAhoiCredentialRequestIncognito()) {
      // OTR must not perform an automatic lookup. The button below is the
      // explicit user gesture that permits a one-shot PasswordStore read.
      saved_accounts_container_->SetVisible(true);
      use_saved_account_button_->SetVisible(true);
    } else {
      login_handler_->LoadSavedCredentials(
          /*explicit_user_selection=*/false,
          base::BindOnce(&LoginView::OnSavedCredentials,
                         weak_ptr_factory_.GetWeakPtr()));
    }
  }
}

LoginView::~LoginView() {
  if (http_auth_manager_) {
    http_auth_manager_->DetachObserver(this);
  }
}

std::u16string_view LoginView::GetUsername() const {
  return username_field_->GetText();
}

std::u16string_view LoginView::GetPassword() const {
  return password_field_->GetText();
}

bool LoginView::ShouldSaveCredential() const {
  return save_radio_ && save_radio_->GetChecked();
}

bool LoginView::ShouldNeverSaveCredential() const {
  return never_save_radio_ && never_save_radio_->GetChecked();
}

views::View* LoginView::GetInitiallyFocusedView() {
  return username_field_;
}

// static
void LoginView::ApplyAhoiDialogFrame(views::DialogDelegate& dialog) {
  ahoi::dialog_style::ApplyDialogFrame(dialog,
                                       ahoi::visual_style::kAuthDialogWidth);
}

// static
void LoginView::ApplyAhoiDialogChrome(views::DialogDelegate& dialog) {
  ahoi::dialog_style::ApplyDialogChrome(dialog);
}

void LoginView::OnAutofillDataAvailable(std::u16string_view username,
                                        std::u16string_view password) {
  if (username_field_->GetText().empty()) {
    username_field_->SetText(username);
    password_field_->SetText(password);
    username_field_->SelectRange(gfx::Range(0, username.size()));
  }
}

void LoginView::OnLoginModelDestroying() {
  http_auth_manager_ = nullptr;
}

void LoginView::OnSavedCredentials(
    std::vector<LoginHandler::SavedCredential> credentials) {
  if (!login_handler_) {
    return;
  }
  saved_credentials_ = std::move(credentials);
  if (saved_credentials_.empty() || !saved_account_model_ ||
      !saved_accounts_container_) {
    return;
  }

  RefreshSavedAccountModel();
  RebuildSavedAccountRows();
  saved_accounts_container_->SetVisible(true);
  use_saved_account_button_->SetVisible(false);
  username_field_->SetText(saved_credentials_.front().username);
  OnUsernameChanged();
}

void LoginView::OnUsernameChanged() {
  // The combobox updates its menu after this callback, so the menu follows
  // the new text.
  RefreshSavedAccountModel();
  const LoginHandler::SavedCredential* credential =
      FindSavedCredential(username_field_->GetText());
  if (!credential) {
    if (selected_saved_username_) {
      password_field_->SetText(std::u16string());
    }
    selected_saved_username_.reset();
    SyncSavedAccountRows();
    delete_armed_username_.reset();
    if (make_preferred_button_) {
      make_preferred_button_->SetVisible(false);
    }
    if (delete_saved_account_button_) {
      delete_saved_account_button_->SetVisible(false);
      delete_saved_account_button_->SetText(
          l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_DELETE_SAVED_ACCOUNT));
    }
    if (save_radio_) {
      save_radio_->SetText(
          l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_SAVE_CREDENTIAL));
    }
    return;
  }
  selected_saved_username_ = credential->username;
  SyncSavedAccountRows();
  delete_armed_username_.reset();
  password_field_->SetText(credential->password);
  make_preferred_button_->SetVisible(!credential->preferred);
  delete_saved_account_button_->SetVisible(true);
  delete_saved_account_button_->SetText(
      l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_DELETE_SAVED_ACCOUNT));
  if (save_radio_) {
    save_radio_->SetText(
        l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_UPDATE_CREDENTIAL));
  }
}

void LoginView::OnUseSavedAccountPressed() {
  if (!login_handler_) {
    return;
  }
  login_handler_->LoadSavedCredentials(
      /*explicit_user_selection=*/true,
      base::BindOnce(&LoginView::OnSavedCredentials,
                     weak_ptr_factory_.GetWeakPtr()));
}

void LoginView::OnMakePreferredPressed() {
  if (!login_handler_ || !selected_saved_username_ ||
      !login_handler_->SetPreferredSavedCredential(*selected_saved_username_)) {
    return;
  }
  for (auto& credential : saved_credentials_) {
    credential.preferred = credential.username == *selected_saved_username_;
  }
  std::stable_sort(saved_credentials_.begin(), saved_credentials_.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.preferred && !rhs.preferred;
                   });
  RefreshSavedAccountModel();
  RebuildSavedAccountRows();
  make_preferred_button_->SetVisible(false);
}

void LoginView::OnDeleteSavedAccountPressed() {
  if (!login_handler_ || !selected_saved_username_) {
    return;
  }
  if (delete_armed_username_ != selected_saved_username_) {
    delete_armed_username_ = selected_saved_username_;
    delete_saved_account_button_->SetText(l10n_util::GetStringUTF16(
        IDS_LOGIN_DIALOG_CONFIRM_DELETE_SAVED_ACCOUNT));
    return;
  }
  const std::u16string username = *selected_saved_username_;
  login_handler_->DeleteSavedCredential(
      username, base::BindOnce(&LoginView::OnSavedAccountDeleted,
                               weak_ptr_factory_.GetWeakPtr(), username));
}

void LoginView::OnSavedAccountDeleted(std::u16string username) {
  std::erase_if(saved_credentials_, [&username](const auto& credential) {
    return credential.username == username;
  });
  RefreshSavedAccountModel();
  selected_saved_username_.reset();
  delete_armed_username_.reset();
  RebuildSavedAccountRows();
  username_field_->SetText(std::u16string());
  password_field_->SetText(std::u16string());
  make_preferred_button_->SetVisible(false);
  delete_saved_account_button_->SetVisible(false);
}

const LoginHandler::SavedCredential* LoginView::FindSavedCredential(
    std::u16string_view username) const {
  const auto it = std::ranges::find(saved_credentials_, username,
                                    &LoginHandler::SavedCredential::username);
  return it == saved_credentials_.end() ? nullptr : &*it;
}

void LoginView::RefreshSavedAccountModel() {
  if (!saved_account_model_) {
    return;
  }
  std::vector<std::u16string_view> usernames;
  usernames.reserve(saved_credentials_.size());
  for (const auto& credential : saved_credentials_) {
    usernames.push_back(credential.username);
  }
  // Every account stays listed while the field is empty or names a saved
  // account (the prefilled preferred one, a menu choice, or the name kept
  // after a failed attempt); other text narrows the list by prefix.
  const std::vector<size_t> entries = ahoi::SelectSavedAccountMenuEntries(
      usernames,
      username_field_ ? username_field_->GetText() : std::u16string_view());
  saved_account_items_.clear();
  saved_account_items_.reserve(entries.size());
  for (size_t index : entries) {
    saved_account_items_.emplace_back(
        saved_credentials_[index].username,
        l10n_util::GetStringFUTF16(IDS_LOGIN_DIALOG_REALM, realm_for_display_),
        favicon_icon_.value_or(ui::ImageModel()));
  }
  saved_account_model_->UpdateItemList(saved_account_items_);
}

void LoginView::RebuildSavedAccountRows() {
  if (!saved_account_rows_) {
    return;
  }
  saved_account_subscriptions_.clear();
  saved_account_radios_.clear();
  saved_account_rows_->RemoveAllChildViews();
  std::vector<std::u16string_view> usernames;
  usernames.reserve(saved_credentials_.size());
  for (const auto& credential : saved_credentials_) {
    usernames.push_back(credential.username);
  }
  // With no text the menu contract lists every account, preferred first;
  // the rows always show all of them.
  const std::vector<size_t> entries =
      ahoi::SelectSavedAccountMenuEntries(usernames, std::u16string_view());
  const std::u16string realm =
      l10n_util::GetStringFUTF16(IDS_LOGIN_DIALOG_REALM, realm_for_display_);
  for (size_t index : entries) {
    auto* radio = saved_account_rows_->AddChildView(
        std::make_unique<views::RadioButton>(saved_credentials_[index].username,
                                             kSavedAccountGroup));
    radio->SetTextSubpixelRenderingEnabled(false);
    radio->SetTooltipText(realm);
    radio->SetMinSize(gfx::Size(0, ahoi::visual_style::kDialogFieldHeight));
    radio->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
        0, ahoi::visual_style::kDialogLabelSpacing)));
    ahoi::dialog_style::StyleFocusRing(*radio);
    saved_account_subscriptions_.push_back(radio->AddCheckedChangedCallback(
        base::BindRepeating(&LoginView::OnSavedAccountRowChecked,
                            base::Unretained(this),
                            saved_account_radios_.size())));
    saved_account_radios_.push_back(radio);
  }
  SyncSavedAccountRows();
}

void LoginView::SyncSavedAccountRows() {
  if (saved_account_radios_.empty()) {
    return;
  }
  syncing_saved_account_rows_ = true;
  for (views::RadioButton* radio : saved_account_radios_) {
    const bool selected = selected_saved_username_ &&
                          radio->GetText() == *selected_saved_username_;
    radio->SetChecked(selected);
    // The chosen account sits on the opaque selection surface.
    radio->SetBackground(
        selected ? views::CreateRoundedRectBackground(
                       ahoi::visual_style::kSelectedSurface,
                       ahoi::visual_style::kRowCornerRadius)
                 : nullptr);
  }
  syncing_saved_account_rows_ = false;
}

void LoginView::OnSavedAccountRowChecked(size_t row) {
  if (syncing_saved_account_rows_ || row >= saved_account_radios_.size() ||
      !saved_account_radios_[row]->GetChecked()) {
    return;
  }
  // Choosing a row fills both fields like choosing it from the menu.
  username_field_->SetText(saved_account_radios_[row]->GetText());
  OnUsernameChanged();
}

void LoginView::OnFaviconAvailable(
    const favicon_base::FaviconImageResult& result) {
  if (result.image.IsEmpty() || !saved_account_model_) {
    return;
  }
  favicon_icon_ = ui::ImageModel::FromImage(result.image);
  if (saved_account_items_.empty()) {
    return;
  }
  const ui::ImageModel& icon = favicon_icon_.value();
  for (auto& item : saved_account_items_) {
    item.icon = icon;
  }
  saved_account_model_->UpdateItemList(saved_account_items_);
}

BEGIN_METADATA(LoginView)
ADD_READONLY_PROPERTY_METADATA(std::u16string_view, Username)
ADD_READONLY_PROPERTY_METADATA(std::u16string_view, Password)
END_METADATA
