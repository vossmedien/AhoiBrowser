// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_presenter.h"

#include "chrome/grit/generated_resources.h"

namespace ahoi::extensions {

namespace {

int ErrorStringId(UboServiceError error) {
  switch (error) {
    case UboServiceError::kUnprovisioned:
      return IDS_AHOI_UBO_UNPROVISIONED;
    case UboServiceError::kOffline:
      return IDS_AHOI_UBO_ERROR_OFFLINE;
    case UboServiceError::kRedirect:
      return IDS_AHOI_UBO_ERROR_REDIRECT;
    case UboServiceError::kResponseTooLarge:
      return IDS_AHOI_UBO_ERROR_OVERSIZE;
    case UboServiceError::kUnexpectedResponse:
      return IDS_AHOI_UBO_ERROR_RESPONSE;
    case UboServiceError::kInvalidCatalog:
      return IDS_AHOI_UBO_ERROR_CATALOG;
    case UboServiceError::kInvalidPackage:
      return IDS_AHOI_UBO_ERROR_PACKAGE;
    case UboServiceError::kRollback:
      return IDS_AHOI_UBO_ERROR_ROLLBACK;
    case UboServiceError::kInstallFailed:
      return IDS_AHOI_UBO_ERROR_INSTALL;
    case UboServiceError::kBusy:
      return IDS_AHOI_UBO_ERROR_BUSY;
    case UboServiceError::kProfileUnavailable:
      return IDS_AHOI_UBO_ERROR_PROFILE;
    case UboServiceError::kNone:
      return IDS_AHOI_UBO_ERROR_RESPONSE;
  }
}

}  // namespace

UboDialogPresentation PresentUboStatus(const UboServiceStatus& status) {
  UboDialogPresentation result;
  result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CHECK;
  switch (status.state) {
    case UboServiceState::kUnprovisioned:
      result.status_string_id = IDS_AHOI_UBO_UNPROVISIONED;
      return result;
    case UboServiceState::kIdle:
      result.status_string_id = IDS_AHOI_UBO_IDLE;
      result.action = UboDialogAction::kCheck;
      result.primary_enabled = true;
      return result;
    case UboServiceState::kCheckingCatalog:
      result.status_string_id = IDS_AHOI_UBO_CHECKING;
      result.show_progress = true;
      return result;
    case UboServiceState::kCatalogReady:
      result.status_string_id = IDS_AHOI_UBO_CATALOG_READY;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_DOWNLOAD;
      result.action = UboDialogAction::kDownload;
      result.primary_enabled = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kUpdateAvailable:
      result.status_string_id = IDS_AHOI_UBO_UPDATE_READY;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_DOWNLOAD;
      result.action = UboDialogAction::kDownload;
      result.primary_enabled = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kDownloadingPackage:
      result.status_string_id = IDS_AHOI_UBO_DOWNLOADING;
      result.show_progress = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kVerifyingPackage:
      result.status_string_id = IDS_AHOI_UBO_VERIFYING;
      result.show_progress = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kPackageReady:
      result.status_string_id = IDS_AHOI_UBO_PACKAGE_READY;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_INSTALL;
      result.action = UboDialogAction::kInstall;
      result.primary_enabled = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kInstalling:
      result.status_string_id = IDS_AHOI_UBO_INSTALLING;
      result.show_progress = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kInstalled:
      result.status_string_id = IDS_AHOI_UBO_INSTALLED;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CLOSE;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kUpToDate:
      result.status_string_id = IDS_AHOI_UBO_UP_TO_DATE;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CLOSE;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      result.show_metadata = status.catalog.has_value();
      return result;
    case UboServiceState::kError:
      result.status_string_id = ErrorStringId(status.error);
      result.show_metadata = status.catalog.has_value();
      if (status.error != UboServiceError::kUnprovisioned &&
          status.error != UboServiceError::kProfileUnavailable &&
          status.error != UboServiceError::kBusy) {
        result.action = UboDialogAction::kCheck;
        result.primary_enabled = true;
      }
      return result;
  }
}

}  // namespace ahoi::extensions
