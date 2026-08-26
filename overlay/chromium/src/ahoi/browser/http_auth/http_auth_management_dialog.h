// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_DIALOG_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_DIALOG_H_

#include <memory>
#include <optional>
#include <vector>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/clipboard/clipboard_sequence_number_token.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace views {
class Button;
class Label;
class MdTextButton;
class ScrollView;
class Textfield;
class View;
}  // namespace views

namespace ahoi {

struct HttpAuthManagementEntry;
class HttpAuthSecretAccessController;

// Window-modal management surface for saved HTTP Basic/Digest credentials. It
// is anchored to one WebContents for its complete lifetime. Password access is
// delegated to HttpAuthSecretAccessController and is never attempted before a
// fresh system authentication succeeds.
class HttpAuthManagementDialog final : public views::DialogDelegate,
                                       public content::WebContentsObserver {
 public:
  HttpAuthManagementDialog(content::WebContents* source_web_contents,
                           HttpAuthCredentialService* service);
  HttpAuthManagementDialog(const HttpAuthManagementDialog&) = delete;
  HttpAuthManagementDialog& operator=(const HttpAuthManagementDialog&) = delete;
  ~HttpAuthManagementDialog() override;

  std::u16string GetWindowTitle() const override;
  void WindowClosing() override;
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;

  size_t visible_credential_count_for_testing() const {
    return visible_credential_count_;
  }
  views::Textfield* search_field_for_testing() const { return search_field_; }
  views::Label* status_label_for_testing() const { return status_label_; }

 private:
  struct PendingDeletion {
    HttpAuthCredentialMetadata metadata;
    bool entire_realm = false;
  };

  void OnSearchChanged();
  void RebuildRows();
  void ScheduleRebuildRows();
  void AddRealmHeader(const HttpAuthCredentialMetadata& metadata);
  void AddCredentialRow(const HttpAuthManagementEntry& entry);
  void AddNeverSaveRow(const HttpAuthProtectionSpace& protection_space);
  void BeginEdit(HttpAuthCredentialMetadata metadata);
  void OnEditSecretLoaded(HttpAuthCredentialMetadata metadata,
                          std::optional<std::u16string> secret);
  void RevealOrHidePassword();
  void OnRevealAuthorized(bool authenticated);
  void CopyPassword();
  void OnCopySecretLoaded(std::optional<std::u16string> secret);
  void SaveEditedCredential();
  void OnSaveAuthorized(bool authenticated);
  void OnCredentialUpdateFinished(bool success);
  void CancelEdit();
  void ClearEditor();
  void ClearCopiedSecretIfUnchanged();
  void MaskEditorPassword();
  void LoadSavedSecret(
      const HttpAuthCredentialMetadata& metadata,
      base::OnceCallback<void(std::optional<std::u16string>)> callback);
  void OnSavedSecretLookupComplete(
      HttpAuthCredentialMetadata metadata,
      base::OnceCallback<void(std::optional<std::u16string>)> callback,
      std::vector<HttpAuthCredential> credentials);
  bool IsSecretContextValid() const;
  bool IsCredentialStillManaged(
      const HttpAuthCredentialMetadata& metadata) const;
  void MakePreferred(HttpAuthCredentialMetadata metadata);
  void SwitchAccount(HttpAuthCredentialMetadata metadata);
  void DeleteCredential(HttpAuthCredentialMetadata metadata);
  void DeleteRealm(HttpAuthCredentialMetadata metadata);
  void ResetNeverSave(HttpAuthProtectionSpace protection_space);
  void OnCredentialDeletionFinished(HttpAuthCredentialMetadata metadata);
  void OnRealmDeletionFinished(HttpAuthProtectionSpace protection_space);
  void ShowStatus(int string_id);
  bool IsDeletionArmed(const HttpAuthCredentialMetadata& metadata,
                       bool entire_realm) const;
  std::optional<HttpAuthProtectionSpace> ActiveProtectionSpace() const;

  const raw_ptr<HttpAuthCredentialService> service_;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::View> editor_container_ = nullptr;
  raw_ptr<views::Label> editor_heading_ = nullptr;
  raw_ptr<views::Textfield> editor_username_field_ = nullptr;
  raw_ptr<views::Textfield> editor_password_field_ = nullptr;
  raw_ptr<views::MdTextButton> reveal_password_button_ = nullptr;
  raw_ptr<views::Button> pending_confirmation_button_ = nullptr;
  base::CallbackListSubscription search_subscription_;
  std::optional<PendingDeletion> pending_deletion_;
  std::optional<HttpAuthCredentialMetadata> editing_metadata_;
  std::unique_ptr<HttpAuthSecretAccessController> secret_access_controller_;
  std::optional<ui::ClipboardSequenceNumberToken> copied_secret_sequence_;
  bool password_revealed_ = false;
  size_t visible_credential_count_ = 0;
  base::WeakPtrFactory<HttpAuthManagementDialog> weak_ptr_factory_{this};
};

// Returns false without creating UI for OTR/non-profile contexts, missing
// services, or destroyed WebContents. Secret actions fail closed when system
// authentication is unavailable.
bool ShowHttpAuthManagementDialog(content::WebContents* source_web_contents);

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_MANAGEMENT_DIALOG_H_
