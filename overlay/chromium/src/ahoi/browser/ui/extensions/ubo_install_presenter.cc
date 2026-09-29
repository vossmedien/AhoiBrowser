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
    case UboServiceError::kConflictingExtension:
      return IDS_AHOI_UBO_ERROR_CONFLICT;
    case UboServiceError::kMigrationStateInvalid:
      return IDS_AHOI_UBO_ERROR_MIGRATION_INTEGRITY;
    case UboServiceError::kMigrationStateWriteFailed:
      return IDS_AHOI_UBO_ERROR_MIGRATION_WRITE;
    case UboServiceError::kLiteRemovalFailed:
      return IDS_AHOI_UBO_ERROR_LITE_REMOVAL;
    case UboServiceError::kNone:
      return IDS_AHOI_UBO_ERROR_RESPONSE;
  }
}

}  // namespace

UboDialogPresentation PresentUboStatus(const UboServiceStatus& status) {
  UboDialogPresentation result;
  result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CLOSE;
  result.show_metadata = status.catalog.has_value();

  switch (status.lite_migration) {
    case UboLiteMigrationState::kClassicAwaitingReady:
      result.status_string_id = IDS_AHOI_UBO_CLASSIC_AWAITING_READY;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboLiteMigrationState::kClassicAwaitingRestart:
      result.status_string_id = IDS_AHOI_UBO_CLASSIC_AWAITING_RESTART;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboLiteMigrationState::kEligibleForLiteRemoval:
      result.status_string_id = IDS_AHOI_UBO_LITE_REMOVAL_ELIGIBLE;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_REMOVE_LITE;
      result.action = UboDialogAction::kRemoveLite;
      result.primary_enabled = true;
      return result;
    case UboLiteMigrationState::kRemovingLite:
      result.status_string_id = IDS_AHOI_UBO_REMOVING_LITE;
      result.show_progress = true;
      return result;
    case UboLiteMigrationState::kComplete:
      result.status_string_id = IDS_AHOI_UBO_LITE_REMOVED;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboLiteMigrationState::kBlocked:
      result.status_string_id = ErrorStringId(status.error);
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboLiteMigrationState::kNone:
      break;
  }

  switch (status.state) {
    case UboServiceState::kUnprovisioned:
      result.status_string_id = IDS_AHOI_UBO_UNPROVISIONED;
      return result;
    case UboServiceState::kIdle:
      if (status.inventory.former_classic_web_store.installed ||
          status.inventory.classic.installed) {
        result.status_string_id = IDS_AHOI_UBO_ERROR_CONFLICT;
        return result;
      }
      result.status_string_id = status.pinned_bootstrap_available
                                    ? IDS_AHOI_UBO_PINNED_BOOTSTRAP_IDLE
                                    : IDS_AHOI_UBO_IDLE;
      if (status.pinned_bootstrap_available && status.catalog) {
        result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_INSTALL;
        result.action = UboDialogAction::kBeginPinnedInstall;
        result.primary_enabled = true;
      }
      return result;
    case UboServiceState::kCheckingCatalog:
      result.status_string_id = IDS_AHOI_UBO_CHECKING;
      result.show_progress = true;
      return result;
    case UboServiceState::kCatalogReady:
      result.status_string_id = IDS_AHOI_UBO_CATALOG_READY;
      if (status.pinned_bootstrap_available) {
        result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_INSTALL;
        result.action = UboDialogAction::kBeginPinnedInstall;
        result.primary_enabled = true;
      }
      return result;
    case UboServiceState::kUpdateAvailable:
      result.status_string_id = IDS_AHOI_UBO_UPDATE_READY;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_DOWNLOAD;
      result.action = UboDialogAction::kDownloadUpdate;
      result.primary_enabled = true;
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
      if (status.one_click_install_in_progress) {
        result.show_progress = true;
      } else {
        result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_INSTALL;
        result.action = UboDialogAction::kInstallPreparedUpdate;
        result.primary_enabled = true;
      }
      return result;
    case UboServiceState::kInstalling:
      result.status_string_id = IDS_AHOI_UBO_INSTALLING;
      result.show_progress = true;
      return result;
    case UboServiceState::kInstalled:
      result.status_string_id = IDS_AHOI_UBO_INSTALLED;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CLOSE;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboServiceState::kUpToDate:
      result.status_string_id = IDS_AHOI_UBO_UP_TO_DATE;
      result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_CLOSE;
      result.action = UboDialogAction::kClose;
      result.primary_enabled = true;
      return result;
    case UboServiceState::kError:
      result.status_string_id = ErrorStringId(status.error);
      if (status.pinned_bootstrap_available && status.catalog &&
          !status.inventory.classic.installed &&
          !status.inventory.former_classic_web_store.installed &&
          (status.error == UboServiceError::kOffline ||
           status.error == UboServiceError::kRedirect ||
           status.error == UboServiceError::kResponseTooLarge ||
           status.error == UboServiceError::kUnexpectedResponse ||
           status.error == UboServiceError::kInvalidPackage ||
           status.error == UboServiceError::kInstallFailed)) {
        result.primary_button_string_id = IDS_AHOI_UBO_BUTTON_INSTALL;
        result.action = UboDialogAction::kBeginPinnedInstall;
        result.primary_enabled = true;
      }
      return result;
  }
}

}  // namespace ahoi::extensions
