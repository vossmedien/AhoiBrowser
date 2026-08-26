// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/extensions/ubo_install_presenter.h"

#include <array>

#include "chrome/grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {

TEST(UboInstallPresenterTest, RequiresExplicitActionAtEveryBoundary) {
  UboServiceStatus status;
  status.state = UboServiceState::kIdle;
  auto idle = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kCheck, idle.action);
  EXPECT_TRUE(idle.primary_enabled);

  status.catalog = UboCatalogEntry();
  status.state = UboServiceState::kCatalogReady;
  auto catalog = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kDownload, catalog.action);
  EXPECT_TRUE(catalog.show_metadata);

  status.state = UboServiceState::kPackageReady;
  auto package = PresentUboStatus(status);
  EXPECT_EQ(UboDialogAction::kInstall, package.action);

  for (UboServiceState busy :
       {UboServiceState::kCheckingCatalog, UboServiceState::kDownloadingPackage,
        UboServiceState::kVerifyingPackage, UboServiceState::kInstalling}) {
    status.state = busy;
    auto presentation = PresentUboStatus(status);
    EXPECT_EQ(UboDialogAction::kNone, presentation.action);
    EXPECT_FALSE(presentation.primary_enabled);
    EXPECT_TRUE(presentation.show_progress);
  }
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
  };
  for (const Case& item : cases) {
    UboServiceStatus status;
    status.state = UboServiceState::kError;
    status.error = item.error;
    auto presentation = PresentUboStatus(status);
    EXPECT_EQ(item.string_id, presentation.status_string_id);
    EXPECT_EQ(UboDialogAction::kCheck, presentation.action);
  }

  UboServiceStatus unprovisioned;
  unprovisioned.state = UboServiceState::kUnprovisioned;
  auto disabled = PresentUboStatus(unprovisioned);
  EXPECT_EQ(UboDialogAction::kNone, disabled.action);
  EXPECT_FALSE(disabled.primary_enabled);
  EXPECT_EQ(IDS_AHOI_UBO_UNPROVISIONED, disabled.status_string_id);
}

}  // namespace ahoi::extensions
