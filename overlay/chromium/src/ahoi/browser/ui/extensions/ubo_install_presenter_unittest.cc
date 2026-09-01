// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_presenter.h"

#include <array>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "chrome/grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {

TEST(UboInstallPresenterTest, PinnedMetadataAndSingleCtaPrecedeNetwork) {
  UboServiceStatus status;
  status.state = UboServiceState::kIdle;
  status.pinned_bootstrap_available = true;
  status.catalog = GetPinnedUboBootstrapCatalogEntry();

  const UboDialogPresentation presentation = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kBeginPinnedInstall, presentation.action);
  EXPECT_EQ(IDS_AHOI_UBO_BUTTON_INSTALL, presentation.primary_button_string_id);
  EXPECT_TRUE(presentation.primary_enabled);
  EXPECT_TRUE(presentation.show_metadata);
  EXPECT_EQ(IDS_AHOI_UBO_PINNED_BOOTSTRAP_IDLE, presentation.status_string_id);
}

TEST(UboInstallPresenterTest, OneClickHasNoSecondAhoiInstallBoundary) {
  UboServiceStatus status;
  status.catalog = GetPinnedUboBootstrapCatalogEntry();
  status.one_click_install_in_progress = true;

  for (UboServiceState busy :
       {UboServiceState::kDownloadingPackage,
        UboServiceState::kVerifyingPackage, UboServiceState::kPackageReady,
        UboServiceState::kInstalling}) {
    status.state = busy;
    const UboDialogPresentation presentation = PresentUboStatus(status);
    EXPECT_EQ(UboDialogAction::kNone, presentation.action);
    EXPECT_FALSE(presentation.primary_enabled);
    EXPECT_TRUE(presentation.show_progress);
    EXPECT_TRUE(presentation.show_metadata);
  }

  status.one_click_install_in_progress = false;
  status.state = UboServiceState::kUpdateAvailable;
  EXPECT_EQ(UboDialogAction::kDownloadUpdate, PresentUboStatus(status).action);
  status.state = UboServiceState::kPackageReady;
  EXPECT_EQ(UboDialogAction::kInstallPreparedUpdate,
            PresentUboStatus(status).action);
}

TEST(UboInstallPresenterTest, LiteRemovalIsSeparateAfterRestartGate) {
  UboServiceStatus status;
  status.state = UboServiceState::kUpToDate;
  status.catalog = GetPinnedUboBootstrapCatalogEntry();

  status.lite_migration = UboLiteMigrationState::kClassicAwaitingReady;
  EXPECT_EQ(UboDialogAction::kClose, PresentUboStatus(status).action);
  EXPECT_EQ(IDS_AHOI_UBO_CLASSIC_AWAITING_READY,
            PresentUboStatus(status).status_string_id);

  status.lite_migration = UboLiteMigrationState::kClassicAwaitingRestart;
  EXPECT_EQ(UboDialogAction::kClose, PresentUboStatus(status).action);
  EXPECT_EQ(IDS_AHOI_UBO_CLASSIC_AWAITING_RESTART,
            PresentUboStatus(status).status_string_id);

  status.lite_migration = UboLiteMigrationState::kEligibleForLiteRemoval;
  const UboDialogPresentation eligible = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kRemoveLite, eligible.action);
  EXPECT_EQ(IDS_AHOI_UBO_BUTTON_REMOVE_LITE, eligible.primary_button_string_id);
  EXPECT_TRUE(eligible.primary_enabled);

  status.lite_migration = UboLiteMigrationState::kRemovingLite;
  const UboDialogPresentation removing = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kNone, removing.action);
  EXPECT_TRUE(removing.show_progress);
}

TEST(UboInstallPresenterTest, MapsSafeLocalizedNegativeStates) {
  struct Case {
    UboServiceError error;
    int string_id;
  };
  constexpr std::array cases{
      Case{UboServiceError::kOffline, IDS_AHOI_UBO_ERROR_OFFLINE},
      Case{UboServiceError::kRedirect, IDS_AHOI_UBO_ERROR_REDIRECT},
      Case{UboServiceError::kResponseTooLarge, IDS_AHOI_UBO_ERROR_OVERSIZE},
      Case{UboServiceError::kInvalidCatalog, IDS_AHOI_UBO_ERROR_CATALOG},
      Case{UboServiceError::kInvalidPackage, IDS_AHOI_UBO_ERROR_PACKAGE},
      Case{UboServiceError::kRollback, IDS_AHOI_UBO_ERROR_ROLLBACK},
      Case{UboServiceError::kInstallFailed, IDS_AHOI_UBO_ERROR_INSTALL},
      Case{UboServiceError::kConflictingExtension, IDS_AHOI_UBO_ERROR_CONFLICT},
      Case{UboServiceError::kMigrationStateInvalid,
           IDS_AHOI_UBO_ERROR_MIGRATION_INTEGRITY},
      Case{UboServiceError::kMigrationStateWriteFailed,
           IDS_AHOI_UBO_ERROR_MIGRATION_WRITE},
      Case{UboServiceError::kLiteRemovalFailed,
           IDS_AHOI_UBO_ERROR_LITE_REMOVAL},
  };
  for (const Case& item : cases) {
    UboServiceStatus status;
    status.state = UboServiceState::kError;
    status.error = item.error;
    const UboDialogPresentation presentation = PresentUboStatus(status);
    EXPECT_EQ(item.string_id, presentation.status_string_id);
    EXPECT_EQ(UboDialogAction::kNone, presentation.action);
  }

  UboServiceStatus retry;
  retry.state = UboServiceState::kError;
  retry.error = UboServiceError::kOffline;
  retry.pinned_bootstrap_available = true;
  retry.catalog = GetPinnedUboBootstrapCatalogEntry();
  EXPECT_EQ(UboDialogAction::kBeginPinnedInstall,
            PresentUboStatus(retry).action);

  UboServiceStatus unprovisioned;
  unprovisioned.state = UboServiceState::kUnprovisioned;
  const UboDialogPresentation disabled = PresentUboStatus(unprovisioned);
  EXPECT_EQ(UboDialogAction::kNone, disabled.action);
  EXPECT_FALSE(disabled.primary_enabled);
  EXPECT_EQ(IDS_AHOI_UBO_UNPROVISIONED, disabled.status_string_id);
}

}  // namespace ahoi::extensions
