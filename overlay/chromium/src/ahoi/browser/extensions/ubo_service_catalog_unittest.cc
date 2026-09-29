// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// UboService tests for the signed-catalog path: bootstrap redirects,
// first-install refusal, verified updates, distinct network errors,
// manipulated catalogs and periodic checks (split from
// ubo_service_unittest.cc, source line budget).

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_service.h"
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

TEST_F(UboServiceTest, PinnedBootstrapRejectsForeignPackageRedirect) {
  auto network = std::make_unique<FakeNetworkClient>();
  network->package_path = package_path_;
  network->package_final_url = GURL("https://attacker.example/ubo.crx");
  auto service =
      MakeService(std::move(network), GetProductionUboProductConfig());

  service->CheckForCatalog(UboCheckReason::kManual);
  service->PreparePackage();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(UboServiceState::kError, service->status().state);
  EXPECT_EQ(UboServiceError::kInvalidPackage, service->status().error);
}

TEST_F(UboServiceTest, SignedCatalogCannotAuthorizeFirstInstall) {
  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->catalog_body = Sign(Payload());
  auto service = MakeService(std::move(network), Config());

  service->CheckForCatalog(UboCheckReason::kManual);

  EXPECT_EQ(UboServiceState::kUnprovisioned, service->status().state);
  EXPECT_EQ(UboServiceError::kUnprovisioned, service->status().error);
  EXPECT_EQ(0, network_ptr->catalog_fetches);
  EXPECT_EQ(0, network_ptr->package_fetches);
}

// The signed-catalog update path retains the same bounded temporary package,
// verifier, explicit install, and atomic authorization hand-off as bootstrap.
TEST_F(UboServiceTest, SignedCatalogExplicitVerifiedUpdateFlow) {
  UboCatalogEntry committed = Entry(41, "1.54.0");
  auto installed = Extension("1.54.0");
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*installed).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(installed));

  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->catalog_body = Sign(Payload());
  network->package_path = package_path_;
  int installs = 0;
  UboInstallFunction installer = base::BindRepeating(
      [](int* installs, Profile* profile, content::WebContents*,
         UboCatalogEntry entry, base::FilePath,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        ++*installs;
        std::array<uint8_t, 32> key;
        key.fill(0x42);
        auto updated =
            ::extensions::ExtensionBuilder("uBlock Origin")
                .SetManifestVersion(2)
                .SetVersion(entry.version.GetString())
                .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
                .SetID(kUboClassicExtensionId)
                .SetManifestKey("key", base::Base64Encode(key))
                .Build();
        auto authorization = BeginUboInstallAuthorization(
            profile->GetPrefs(), entry,
            VerifiedUboPackage{
                .extension_id = entry.extension_id,
                .package_sha256 = entry.package_sha256,
                .crx_public_key_sha256 = entry.crx_public_key_sha256});
        if (!authorization.has_value() ||
            !(*authorization)->Commit(*updated).has_value()) {
          std::move(callback).Run(
              base::unexpected(UboVerificationError::kStateWriteFailed));
          return UboInstallOperationPtr();
        }
        ::extensions::ExtensionRegistry::Get(profile)->AddEnabled(updated);
        std::move(callback).Run(base::ok());
        return UboInstallOperationPtr();
      },
      &installs);
  auto service = MakeService(std::move(network), Config(), UboPackageVerifier(),
                             std::move(installer));

  service->CheckForCatalog(UboCheckReason::kManual);
  ASSERT_EQ(UboServiceState::kUpdateAvailable, service->status().state);
  service->PreparePackage();
  task_environment_.RunUntilIdle();
  ASSERT_EQ(UboServiceState::kPackageReady, service->status().state);
  EXPECT_EQ(1, network_ptr->package_fetches);

  auto web_contents = content::WebContentsTester::CreateTestWebContents(
      profile_.get(), nullptr);
  service->InstallPreparedPackage(web_contents.get());
  EXPECT_EQ(1, installs);
  EXPECT_EQ(UboServiceState::kInstalled, service->status().state);
}

TEST_F(UboServiceTest, RedirectOversizeAndOfflineAreDistinctSafeErrors) {
  UboCatalogEntry committed = Entry(41, "1.54.0");
  auto installed = Extension("1.54.0");
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*installed).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(installed));

  for (const auto& [network_error, service_error] : std::array{
           std::pair{UboNetworkError::kRedirect, UboServiceError::kRedirect},
           std::pair{UboNetworkError::kResponseTooLarge,
                     UboServiceError::kResponseTooLarge},
           std::pair{UboNetworkError::kOffline, UboServiceError::kOffline}}) {
    auto network = std::make_unique<FakeNetworkClient>();
    network->catalog_error = network_error;
    auto service = MakeService(std::move(network), Config());
    service->CheckForCatalog(UboCheckReason::kManual);
    EXPECT_EQ(UboServiceState::kError, service->status().state);
    EXPECT_EQ(service_error, service->status().error);
  }
}

TEST_F(UboServiceTest, ManipulatedCatalogAndPackageHashFailClosed) {
  UboCatalogEntry committed = Entry(41, "1.54.0");
  auto installed = Extension("1.54.0");
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*installed).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(installed));

  auto catalog_network = std::make_unique<FakeNetworkClient>();
  catalog_network->catalog_body = Sign(Payload());
  catalog_network->catalog_body.replace(
      catalog_network->catalog_body.find("1.55.0"), 6, "1.54.0");
  auto catalog_service = MakeService(std::move(catalog_network), Config());
  catalog_service->CheckForCatalog(UboCheckReason::kManual);
  EXPECT_EQ(UboServiceError::kInvalidCatalog, catalog_service->status().error);

  auto package_network = std::make_unique<FakeNetworkClient>();
  package_network->catalog_body = Sign(Payload());
  package_network->package_path = package_path_;
  UboPackageVerifier bad_verifier =
      base::BindRepeating([](const UboCatalogEntry&, const base::FilePath&) {
        return base::expected<VerifiedUboPackage, UboVerificationError>(
            base::unexpected(UboVerificationError::kPackageHashMismatch));
      });
  auto package_service = MakeService(std::move(package_network), Config(),
                                     std::move(bad_verifier));
  package_service->CheckForCatalog(UboCheckReason::kManual);
  package_service->PreparePackage();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(UboServiceError::kInvalidPackage, package_service->status().error);
}

TEST_F(UboServiceTest, UBO09SignedCatalogMustAdvancePinnedBootstrapSequence) {
  UboCatalogEntry committed = GetPinnedUboBootstrapCatalogEntry();
  auto installed = PinnedBootstrapExtension();
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*installed).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(installed));

  auto update_network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* update_network_ptr = update_network.get();
  base::DictValue update = Payload(kUboClassicBootstrapSequence + 1, "1.75.0");
  update.Set("upstream_tag", "1.75.0");
  update.Set("upstream_source_url",
             "https://github.com/gorhill/uBlock/releases/tag/1.75.0");
  update_network->catalog_body = Sign(std::move(update));
  auto update_service = MakeService(std::move(update_network), Config());
  update_service->CheckForCatalog(UboCheckReason::kManual);

  EXPECT_EQ(UboServiceState::kUpdateAvailable, update_service->status().state);
  EXPECT_EQ(1, update_network_ptr->catalog_fetches);
  EXPECT_EQ(0, update_network_ptr->package_fetches);

  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  base::DictValue rollback =
      Payload(kUboClassicBootstrapSequence - 1, "1.75.0");
  rollback.Set("upstream_tag", "1.75.0");
  rollback.Set("upstream_source_url",
               "https://github.com/gorhill/uBlock/releases/tag/1.75.0");
  network->catalog_body = Sign(std::move(rollback));
  auto service = MakeService(std::move(network), Config());
  service->CheckForCatalog(UboCheckReason::kManual);

  EXPECT_EQ(UboServiceError::kRollback, service->status().error);
  EXPECT_EQ(0, network_ptr->package_fetches);
}

// Later signed-catalog operation is metadata-only and exists only after the
// fixed package is installed and its local authorization is committed.
TEST_F(UboServiceTest, SignedCatalogPeriodicCheckNeverDownloadsOrInstalls) {
  UboCatalogEntry committed = Entry();
  auto extension = Extension();
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*extension).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(extension));

  auto network = std::make_unique<FakeNetworkClient>();
  FakeNetworkClient* network_ptr = network.get();
  network->catalog_body = Sign(Payload());
  int installs = 0;
  UboInstallFunction installer = base::BindRepeating(
      [](int* installs, Profile*, content::WebContents*, UboCatalogEntry,
         base::FilePath, UboInstallCallback) -> UboInstallOperationPtr {
        ++*installs;
        return nullptr;
      },
      &installs);
  auto service = MakeService(std::move(network), Config(), UboPackageVerifier(),
                             std::move(installer));
  ASSERT_TRUE(service->IsPeriodicCheckEnabled());

  service->RunPeriodicCheckForTesting();
  EXPECT_EQ(1, network_ptr->catalog_fetches);
  EXPECT_EQ(0, network_ptr->package_fetches);
  EXPECT_EQ(0, installs);
  EXPECT_EQ(UboServiceState::kUpToDate, service->status().state);
}

TEST_F(UboServiceTest, UninstallClearsAuthorizationAndPeriodicEligibility) {
  UboCatalogEntry committed = Entry();
  auto extension = Extension();
  auto authorization = BeginUboInstallAuthorization(
      profile_->GetPrefs(), committed,
      VerifiedUboPackage{
          .extension_id = committed.extension_id,
          .package_sha256 = committed.package_sha256,
          .crx_public_key_sha256 = committed.crx_public_key_sha256});
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*extension).has_value());
  ASSERT_TRUE(::extensions::ExtensionRegistry::Get(profile_.get())
                  ->AddEnabled(extension));
  auto service = MakeService(std::make_unique<FakeNetworkClient>(), Config());
  ASSERT_TRUE(service->IsPeriodicCheckEnabled());

  service->OnExtensionUninstalled(
      profile_.get(), extension.get(),
      ::extensions::UNINSTALL_REASON_USER_INITIATED);
  EXPECT_FALSE(ReadCommittedUboAuthorization(*profile_->GetPrefs()));
  EXPECT_FALSE(service->IsPeriodicCheckEnabled());
}

}  // namespace

}  // namespace ahoi::extensions
