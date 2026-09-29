// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_service_unittest_support.h"
#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "crypto/hash.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {

namespace {

using test_support::FakeNetworkClient;
using test_support::HangingInstallOperation;
using test_support::kNowSeconds;
using test_support::kPackageHash;
using test_support::kPinnedCrxPublicKeyBase64;
using test_support::kUpstreamCommit;
using test_support::UboServiceTest;

// UBO-13: a build without the external trust roots presents a disabled state
// and performs no network request.
TEST_F(UboServiceTest, UBO13UnprovisionedFailsClosedWithoutNetwork) {
  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  auto service = MakeService(std::move(network), UboProductConfig());

  EXPECT_EQ(UboServiceState::kUnprovisioned, service->status().state);
  service->CheckForCatalog(UboCheckReason::kManual);
  EXPECT_EQ(0, network_ptr->catalog_fetches);
}

TEST_F(UboServiceTest, UBO01PinnedOfficialGithubBootstrapSkipsCatalogNetwork) {
  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->package_path = package_path_;
  network->package_final_url = GURL(
      base::StrCat({"https://release-assets.githubusercontent.com",
                    kUboClassicReleaseAssetPath, "?jwt=signed-by-github"}));
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());
  ASSERT_EQ(UboServiceState::kIdle, service->status().state);
  EXPECT_TRUE(service->status().pinned_bootstrap_available);

  service->CheckForCatalog(UboCheckReason::kManual);
  ASSERT_EQ(UboServiceState::kCatalogReady, service->status().state);
  ASSERT_TRUE(service->status().catalog);
  EXPECT_TRUE(IsPinnedUboBootstrapCatalogEntry(*service->status().catalog));
  EXPECT_EQ(0, network_ptr->catalog_fetches);

  service->PreparePackage();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, network_ptr->package_fetches);
  EXPECT_EQ(UboServiceState::kPackageReady, service->status().state);
}

TEST_F(UboServiceTest,
       OneClickDownloadsVerifiesAndHandsOffWithoutCatalogOrSecondCta) {
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(lite));

  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->package_path = package_path_;
  network->package_final_url = GURL(
      base::StrCat({"https://release-assets.githubusercontent.com",
                    kUboClassicReleaseAssetPath, "?jwt=signed-by-github"}));
  int installs = 0;
  base::FilePath handed_off_package;
  UboInstallCallback prompt_callback;
  UboInstallFunction installer = base::BindRepeating(
      [](int* installs, base::FilePath* handed_off_package,
         UboInstallCallback* prompt_callback, Profile*, content::WebContents*,
         UboCatalogEntry, base::FilePath package,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        ++*installs;
        *handed_off_package = std::move(package);
        *prompt_callback = std::move(callback);
        return nullptr;
      },
      &installs, &handed_off_package, &prompt_callback);
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig(),
                  UboPackageVerifier(), std::move(installer));
  ASSERT_TRUE(service->status().catalog);
  EXPECT_TRUE(IsPinnedUboBootstrapCatalogEntry(*service->status().catalog));

  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);
  service->BeginPinnedBootstrapInstall(web_contents.get());
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, network_ptr->catalog_fetches);
  EXPECT_EQ(1, network_ptr->package_fetches);
  EXPECT_EQ(1, installs);
  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  EXPECT_TRUE(service->status().one_click_install_in_progress);

  // After handoff Chromium owns cancellation; closing Ahoi's status surface
  // must not race or bypass the normal permission prompt.
  service->CancelUserInstall();
  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  ASSERT_TRUE(prompt_callback);
  std::move(prompt_callback)
      .Run(base::unexpected(UboVerificationError::kInstallFailed));
  EXPECT_EQ(UboServiceState::kError, service->status().state);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  EXPECT_TRUE(base::DeleteFile(handed_off_package));
}

TEST_F(UboServiceTest,
       ShutdownCancelsNoCallbackInstallAndReleasesTemporaryPackage) {
  auto network = std::make_unique<FakeNetworkClient>();
  network->package_path = package_path_;
  network->package_final_url = GURL(
      base::StrCat({"https://release-assets.githubusercontent.com",
                    kUboClassicReleaseAssetPath, "?jwt=signed-by-github"}));
  int cancellation_count = 0;
  bool terminal_callback_run = false;
  UboInstallFunction installer = base::BindRepeating(
      [](int* cancellation_count, bool* terminal_callback_run, Profile*,
         content::WebContents*, UboCatalogEntry, base::FilePath package,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        return std::make_unique<HangingInstallOperation>(
            std::move(package), std::move(callback), cancellation_count,
            terminal_callback_run);
      },
      &cancellation_count, &terminal_callback_run);
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig(),
                  UboPackageVerifier(), std::move(installer));
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(UboServiceState::kInstalling, service->status().state);
  ASSERT_TRUE(base::PathExists(package_path_));

  service->Shutdown();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, cancellation_count);
  EXPECT_TRUE(terminal_callback_run);
  EXPECT_FALSE(base::PathExists(package_path_));
}

TEST_F(UboServiceTest,
       CancelWhileDialogOwnsPreHandoffDownloadDiscardsLatePackage) {
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(lite));

  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->defer_package = true;
  network->package_path = package_path_;
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get(),
                                       /*wait_for_install_dialog_close=*/true);
  ASSERT_EQ(UboServiceState::kDownloadingPackage, service->status().state);
  EXPECT_FALSE(service->status().prompt_handoff_pending);
  service->CancelUserInstall();
  EXPECT_EQ(1, network_ptr->cancellations);
  EXPECT_EQ(UboServiceState::kIdle, service->status().state);
  EXPECT_FALSE(service->status().one_click_install_in_progress);

  network_ptr->CompleteDeferredPackage();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(package_path_));
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
}

TEST_F(UboServiceTest, LostTabBeforePromptFailsClosedAndDeletesPackage) {
  auto network = std::make_unique<FakeNetworkClient>();
  network->package_path = package_path_;
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get());
  web_contents.reset();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(UboServiceState::kError, service->status().state);
  EXPECT_EQ(UboServiceError::kProfileUnavailable, service->status().error);
  EXPECT_FALSE(base::PathExists(package_path_));
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
}

TEST_F(UboServiceTest,
       HashAndKeyVerificationFailuresKeepLiteWithoutSecurityState) {
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(lite));
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  {
    auto network = std::make_unique<FakeNetworkClient>();
    network->package_path = package_path_;
    UboPackageVerifier bad_hash =
        base::BindRepeating([](const UboCatalogEntry&, const base::FilePath&) {
          return base::expected<VerifiedUboPackage, UboVerificationError>(
              base::unexpected(UboVerificationError::kPackageHashMismatch));
        });
    auto service =
        MakeService(std::move(network), GetProductionUboProductConfig(),
                    std::move(bad_hash));
    service->BeginPinnedBootstrapInstall(web_contents.get());
    task_environment_.RunUntilIdle();

    EXPECT_EQ(UboServiceError::kInvalidPackage, service->status().error);
    EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
    EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }

  ASSERT_TRUE(base::WriteFile(package_path_, "test"));
  {
    auto network = std::make_unique<FakeNetworkClient>();
    network->package_path = package_path_;
    UboPackageVerifier bad_key = base::BindRepeating(
        [](const UboCatalogEntry& entry, const base::FilePath&) {
          return base::expected<VerifiedUboPackage, UboVerificationError>(
              VerifiedUboPackage{
                  .extension_id = entry.extension_id,
                  .package_sha256 = entry.package_sha256,
                  .crx_public_key_sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                                           "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
              });
        });
    auto service =
        MakeService(std::move(network), GetProductionUboProductConfig(),
                    std::move(bad_key));
    service->BeginPinnedBootstrapInstall(web_contents.get());
    task_environment_.RunUntilIdle();

    EXPECT_EQ(UboServiceError::kInvalidPackage, service->status().error);
    EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
    EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }
}

TEST_F(UboServiceTest, FormerClassicBlocksInitialOneClickByExactIdentity) {
  auto former = ExtensionWithId(kUboFormerClassicWebStoreExtensionId, "1.60.0",
                                /*manifest_version=*/2);
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(former));
  ASSERT_TRUE(registry->AddEnabled(lite));
  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get());

  EXPECT_EQ(UboServiceError::kConflictingExtension, service->status().error);
  EXPECT_EQ(0, network_ptr->catalog_fetches);
  EXPECT_EQ(0, network_ptr->package_fetches);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(
      registry->GetInstalledExtension(kUboFormerClassicWebStoreExtensionId));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
}

TEST_F(UboServiceTest,
       UnauthorizedPinnedClassicBlocksInitialOneClickByExactIdentity) {
  auto classic = PinnedBootstrapExtension();
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(classic));
  ASSERT_TRUE(registry->AddEnabled(lite));
  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get());

  EXPECT_EQ(UboServiceError::kConflictingExtension, service->status().error);
  EXPECT_EQ(0, network_ptr->catalog_fetches);
  EXPECT_EQ(0, network_ptr->package_fetches);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboClassicExtensionId));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
}

TEST_F(UboServiceTest,
       RegistryInstallBeforeAuthorizationCommitDoesNotFakeMigrationError) {
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto classic = PinnedBootstrapExtension();
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(lite));
  auto network = std::make_unique<FakeNetworkClient>();
  network->package_path = package_path_;
  UboService* service_ptr = nullptr;
  base::FilePath handed_off_package;
  UboInstallCallback prompt_callback;
  UboInstallFunction installer = base::BindRepeating(
      [](UboService** service_ptr,
         scoped_refptr<const ::extensions::Extension> classic,
         base::FilePath* handed_off_package,
         UboInstallCallback* prompt_callback, Profile* profile,
         content::WebContents*, UboCatalogEntry, base::FilePath package,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        auto* registry = ::extensions::ExtensionRegistry::Get(profile);
        if (!registry || !registry->AddEnabled(classic)) {
          ADD_FAILURE() << "test extension could not enter the registry";
          std::move(callback).Run(
              base::unexpected(UboVerificationError::kInstallFailed));
          return nullptr;
        }
        (*service_ptr)->OnExtensionInstalled(profile, classic.get(), false);
        *handed_off_package = std::move(package);
        *prompt_callback = std::move(callback);
        return nullptr;
      },
      &service_ptr, classic, &handed_off_package, &prompt_callback);
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig(),
                  UboPackageVerifier(), std::move(installer));
  service_ptr = service.get();
  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);

  service->BeginPinnedBootstrapInstall(web_contents.get());
  task_environment_.RunUntilIdle();

  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  EXPECT_NE(UboServiceError::kMigrationStateInvalid, service->status().error);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  ASSERT_TRUE(prompt_callback);
  std::move(prompt_callback)
      .Run(base::unexpected(UboVerificationError::kInstallFailed));
  EXPECT_EQ(UboServiceError::kInstallFailed, service->status().error);
  EXPECT_NE(UboServiceError::kMigrationStateInvalid, service->status().error);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile_->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  EXPECT_TRUE(base::DeleteFile(handed_off_package));
}

TEST_F(UboServiceTest, InventoriesPinnedFormerAndLiteByExactIdentity) {
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  auto former = ExtensionWithId(kUboFormerClassicWebStoreExtensionId, "1.60.0",
                                /*manifest_version=*/2);
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  ASSERT_TRUE(registry->AddDisabled(former));
  ASSERT_TRUE(registry->AddEnabled(lite));
  ASSERT_TRUE(registry->AddReady(lite));

  auto service = MakeService(std::make_unique<FakeNetworkClient>(),
                             GetProductionUboProductConfig());
  EXPECT_FALSE(service->status().inventory.classic.installed);
  EXPECT_TRUE(service->status().inventory.former_classic_web_store.installed);
  EXPECT_FALSE(service->status().inventory.former_classic_web_store.enabled);
  EXPECT_EQ("1.60.0",
            service->status().inventory.former_classic_web_store.version);
  EXPECT_TRUE(service->status().inventory.lite.installed);
  EXPECT_TRUE(service->status().inventory.lite.enabled);
  EXPECT_TRUE(service->status().inventory.lite.ready);
  EXPECT_EQ("2026.8.31.0", service->status().inventory.lite.version);
}

TEST_F(UboServiceTest, LiteRemovalRequiresReadyClassicInLaterBrowserProcess) {
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  auto classic = PinnedBootstrapExtension();
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), entry,
      VerifiedUboPackage{.extension_id = entry.extension_id,
                         .package_sha256 = entry.package_sha256,
                         .crx_public_key_sha256 = entry.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*classic).has_value());
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(classic));
  ASSERT_TRUE(registry->AddReady(classic));
  ASSERT_TRUE(registry->AddEnabled(lite));
  ASSERT_TRUE(WriteUboPersistedMigrationState(
      profile_->GetPrefs(),
      *ReadCommittedUboAuthorization(*profile_->GetPrefs()), "process-a"));

  {
    auto install_process = MakeService(
        std::make_unique<FakeNetworkClient>(), GetProductionUboProductConfig(),
        UboPackageVerifier(), UboInstallFunction(), "process-a");
    EXPECT_EQ(UboLiteMigrationState::kClassicAwaitingRestart,
              install_process->status().lite_migration);
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }

  {
    auto same_browser_process = MakeService(
        std::make_unique<FakeNetworkClient>(), GetProductionUboProductConfig(),
        UboPackageVerifier(), UboInstallFunction(), "process-a");
    EXPECT_EQ(UboLiteMigrationState::kClassicAwaitingRestart,
              same_browser_process->status().lite_migration);
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }

  auto later_process = MakeService(
      std::make_unique<FakeNetworkClient>(), GetProductionUboProductConfig(),
      UboPackageVerifier(), UboInstallFunction(), "process-b");
  EXPECT_EQ(UboLiteMigrationState::kEligibleForLiteRemoval,
            later_process->status().lite_migration);
  // Eligibility is informational until the user invokes the distinct removal
  // action. Service construction and install success never remove or disable
  // Lite.
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  EXPECT_TRUE(registry->enabled_extensions().Contains(kUboLiteExtensionId));
}

TEST_F(UboServiceTest, MalformedMigrationStateIsIntegrityBlockedAndKeepsLite) {
  profile_->GetPrefs()->SetDict(
      kUboMigrationPref,
      base::DictValue().Set("schema_version", 999).Set("tampered", true));
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(lite));

  auto service = MakeService(std::make_unique<FakeNetworkClient>(),
                             GetProductionUboProductConfig());
  EXPECT_EQ(UboLiteMigrationState::kBlocked, service->status().lite_migration);
  EXPECT_EQ(UboServiceError::kMigrationStateInvalid, service->status().error);
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
}

TEST_F(UboServiceTest, MismatchedMigrationStateIsIntegrityBlockedAndKeepsLite) {
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  auto classic = PinnedBootstrapExtension();
  auto lite = ExtensionWithId(kUboLiteExtensionId, "2026.8.31.0",
                              /*manifest_version=*/3);
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), entry,
      VerifiedUboPackage{.extension_id = entry.extension_id,
                         .package_sha256 = entry.package_sha256,
                         .crx_public_key_sha256 = entry.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*classic).has_value());
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
  ASSERT_TRUE(registry->AddEnabled(classic));
  ASSERT_TRUE(registry->AddReady(classic));
  ASSERT_TRUE(registry->AddEnabled(lite));
  ASSERT_TRUE(WriteUboPersistedMigrationState(
      profile_->GetPrefs(),
      *ReadCommittedUboAuthorization(*profile_->GetPrefs()), "process-a"));
  base::DictValue mismatched =
      profile_->GetPrefs()->GetDict(kUboMigrationPref).Clone();
  mismatched.Set("version", "1.73.0");
  profile_->GetPrefs()->SetDict(kUboMigrationPref, std::move(mismatched));

  auto service = MakeService(
      std::make_unique<FakeNetworkClient>(), GetProductionUboProductConfig(),
      UboPackageVerifier(), UboInstallFunction(), "process-b");
  EXPECT_EQ(UboLiteMigrationState::kBlocked, service->status().lite_migration);
  EXPECT_EQ(UboServiceError::kMigrationStateInvalid, service->status().error);
  EXPECT_NE(UboServiceError::kMigrationStateWriteFailed,
            service->status().error);
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
}

}  // namespace

}  // namespace ahoi::extensions
