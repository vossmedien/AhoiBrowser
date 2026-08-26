// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <string>

#include "ahoi/browser/http_auth/http_auth_credential_service_factory.h"
#include "ahoi/browser/http_auth/http_auth_management_dialog.h"
#include "ahoi/browser/http_auth/http_auth_management_model.h"
#include "ahoi/browser/http_auth/http_auth_secret_access_controller.h"
#include "ahoi/browser/http_auth/http_auth_session_controller.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

namespace ahoi {

HttpAuthManagementDialog::~HttpAuthManagementDialog() {
  ClearEditor();
  secret_access_controller_.reset();
}

std::u16string HttpAuthManagementDialog::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_AHOI_HTTP_AUTH_MANAGER_TITLE);
}

void HttpAuthManagementDialog::WindowClosing() {
  ClearEditor();
  secret_access_controller_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void HttpAuthManagementDialog::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle || !navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }
  ClearEditor();
  secret_access_controller_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void HttpAuthManagementDialog::WebContentsDestroyed() {
  ClearEditor();
  secret_access_controller_.reset();
  Observe(nullptr);
  pending_deletion_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void HttpAuthManagementDialog::OnSearchChanged() {
  ClearEditor();
  pending_deletion_.reset();
  RebuildRows();
}

void HttpAuthManagementDialog::ScheduleRebuildRows() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&HttpAuthManagementDialog::RebuildRows,
                                weak_ptr_factory_.GetWeakPtr()));
}

void HttpAuthManagementDialog::OnCredentialUpdateFinished(bool success) {
  ShowStatus(success ? IDS_AHOI_HTTP_AUTH_MANAGER_ACCOUNT_UPDATED
                     : IDS_AHOI_HTTP_AUTH_MANAGER_ACTION_FAILED);
  ScheduleRebuildRows();
}

void HttpAuthManagementDialog::CancelEdit() {
  ClearEditor();
  if (GetWidget()) {
    GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
  }
}

void HttpAuthManagementDialog::ShowStatus(int string_id) {
  status_label_->SetText(l10n_util::GetStringUTF16(string_id));
  status_label_->SetVisible(true);
}

bool HttpAuthManagementDialog::IsDeletionArmed(
    const HttpAuthCredentialMetadata& metadata,
    bool entire_realm) const {
  if (!pending_deletion_ || pending_deletion_->entire_realm != entire_realm) {
    return false;
  }
  return entire_realm ? IsSameManagedHttpAuthRealm(
                            pending_deletion_->metadata.protection_space,
                            metadata.protection_space)
                      : IsSameManagedHttpAuthCredential(
                            pending_deletion_->metadata, metadata);
}

std::optional<HttpAuthProtectionSpace>
HttpAuthManagementDialog::ActiveProtectionSpace() const {
  HttpAuthSessionController* controller =
      web_contents()
          ? HttpAuthSessionController::FromWebContents(web_contents())
          : nullptr;
  return controller ? controller->active_protection_space() : std::nullopt;
}

bool ShowHttpAuthManagementDialog(content::WebContents* source_web_contents) {
  if (!source_web_contents || !source_web_contents->GetTopLevelNativeWindow()) {
    return false;
  }
  Profile* profile =
      Profile::FromBrowserContext(source_web_contents->GetBrowserContext());
  if (!profile || !profile->IsRegularProfile()) {
    return false;
  }
  HttpAuthCredentialService* service =
      HttpAuthCredentialServiceFactory::GetForProfile(profile);
  if (!service) {
    return false;
  }
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::make_unique<HttpAuthManagementDialog>(source_web_contents, service),
      source_web_contents->GetTopLevelNativeWindow());
  if (!widget) {
    return false;
  }
  widget->Show();
  return true;
}

}  // namespace ahoi
