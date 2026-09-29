// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOGIN_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOGIN_VIEW_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ui/login/login_handler.h"
#include "components/password_manager/core/browser/http_auth_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/view.h"

namespace views {
class EditableCombobox;
class MdTextButton;
class RadioButton;
class Textfield;
}  // namespace views

namespace favicon_base {
struct FaviconImageResult;
}

namespace password_manager {
class HttpAuthManager;
}

// This class is responsible for displaying the contents of a login window
// for HTTP/FTP authentication.
class LoginView : public views::View,
                  public password_manager::HttpAuthObserver {
  METADATA_HEADER(LoginView, views::View)

 public:
  // When generic credential management is allowed,
  // |login_model_data->model| is observed for the lifetime of the LoginView.
  // Otherwise the model is deliberately not retained. |login_model_data| may
  // be null.
  LoginView(const std::u16string& authority,
            const std::u16string& explanation,
            LoginHandler::LoginModelData* login_model_data);
  LoginView(const LoginView&) = delete;
  LoginView& operator=(const LoginView&) = delete;
  ~LoginView() override;

  // Access the data in the username/password text fields.
  std::u16string_view GetUsername() const;
  std::u16string_view GetPassword() const;

  bool ShouldSaveCredential() const;
  bool ShouldNeverSaveCredential() const;

  // password_manager::HttpAuthObserver:
  void OnAutofillDataAvailable(std::u16string_view username,
                               std::u16string_view password) override;
  void OnLoginModelDestroying() override;

  // Used by LoginHandlerViews to set the initial focus.
  views::View* GetInitiallyFocusedView();

 private:
  void OnSavedCredentials(
      std::vector<LoginHandler::SavedCredential> credentials);
  void OnUsernameChanged();
  void OnUseSavedAccountPressed();
  void OnMakePreferredPressed();
  void OnDeleteSavedAccountPressed();
  void OnSavedAccountDeleted(std::u16string username);
  void OnFaviconAvailable(const favicon_base::FaviconImageResult& result);
  const LoginHandler::SavedCredential* FindSavedCredential(
      std::u16string_view username) const;
  void RefreshSavedAccountModel();

  // Non-owning refs to the input text fields.
  raw_ptr<views::EditableCombobox> username_field_;
  raw_ptr<views::Textfield> password_field_;

  base::WeakPtr<LoginHandler> login_handler_;
  std::u16string realm_for_display_;
  raw_ptr<views::View> saved_accounts_container_ = nullptr;
  raw_ptr<views::MdTextButton> use_saved_account_button_ = nullptr;
  raw_ptr<views::MdTextButton> make_preferred_button_ = nullptr;
  raw_ptr<views::MdTextButton> delete_saved_account_button_ = nullptr;
  raw_ptr<views::RadioButton> save_radio_ = nullptr;
  raw_ptr<views::RadioButton> never_save_radio_ = nullptr;
  raw_ptr<ui::SimpleComboboxModel> saved_account_model_ = nullptr;
  std::vector<LoginHandler::SavedCredential> saved_credentials_;
  std::vector<ui::SimpleComboboxModel::Item> saved_account_items_;
  std::optional<std::u16string> selected_saved_username_;
  std::optional<std::u16string> delete_armed_username_;
  std::optional<ui::ImageModel> favicon_icon_;
  base::CancelableTaskTracker favicon_task_tracker_;

  // If not null, points to a model we need to notify of our own destruction
  // so it doesn't try and access this when its too late.
  raw_ptr<password_manager::HttpAuthManager> http_auth_manager_;

  base::WeakPtrFactory<LoginView> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOGIN_VIEW_H_
