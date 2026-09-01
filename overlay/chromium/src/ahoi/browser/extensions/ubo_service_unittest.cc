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

constexpr int64_t kNowSeconds = 2000000000;
constexpr char kPackageHash[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kUpstreamCommit[] = "cccccccccccccccccccccccccccccccccccccccc";
constexpr char kPinnedCrxPublicKeyBase64[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsgdkJHEX8xHAytYy3Rih"
    "qn5FoU/cbPKhoorkCCsgF8HR2y2OSWGM1Ojrmnr0ebgM9WA1pl1hr1CmOH7DjgQ"
    "VKRhzBjK7/Zb6RVJNPVGEQvV9CdCUwOTKsu1qQGRbjm9Z/DkYxgu6B2sLo0ZpQ/"
    "IBsmBvs+FGR4CqrWra8GZPwn7n3FibeoxcArWiAx85N2Oyiaef2Geytoog4hS+I"
    "5Fs3ymKkEeTYM3tzeC0U5nZ010LCnlQe0cQ3UDOro8VzLosuhaxAsrPFErIOfIUf"
    "vV3sNhQJrySqgii9Xv6RWT8TI3pHL1yjevKKTxNb2VbPlTOi5MyzPowWV8hHJEO"
    "kwq2dQIDAQAB";

class FakeNetworkClient final : public UboNetworkClient {
 public:
  void FetchCatalog(const GURL& exact_url,
                    UboCatalogDownloadCallback callback) override {
    ++catalog_fetches;
    if (catalog_error) {
      std::move(callback).Run(base::unexpected(*catalog_error));
      return;
    }
    std::move(callback).Run(UboCatalogDownload{
        catalog_final_url.is_valid() ? catalog_final_url : exact_url,
        catalog_body});
  }

  void FetchPackage(const GURL& exact_url,
                    UboDownloadProgressCallback progress,
                    UboPackageDownloadCallback callback) override {
    ++package_fetches;
    if (defer_package) {
      deferred_package_url = exact_url;
      deferred_package_progress = std::move(progress);
      deferred_package_callback = std::move(callback);
      return;
    }
    if (package_error) {
      std::move(callback).Run(base::unexpected(*package_error));
      return;
    }
    if (progress) {
      progress.Run(4);
    }
    std::move(callback).Run(UboPackageDownload{
        package_final_url.is_valid() ? package_final_url : exact_url,
        package_path});
  }

  void Cancel() override { ++cancellations; }

  void CompleteDeferredPackage() {
    ASSERT_TRUE(deferred_package_callback);
    if (deferred_package_progress) {
      deferred_package_progress.Run(4);
    }
    std::move(deferred_package_callback)
        .Run(UboPackageDownload{package_final_url.is_valid()
                                    ? package_final_url
                                    : deferred_package_url,
                                package_path});
  }

  int catalog_fetches = 0;
  int package_fetches = 0;
  int cancellations = 0;
  bool defer_package = false;
  std::optional<UboNetworkError> catalog_error;
  std::optional<UboNetworkError> package_error;
  GURL catalog_final_url;
  GURL package_final_url;
  std::string catalog_body;
  base::FilePath package_path;
  GURL deferred_package_url;
  UboDownloadProgressCallback deferred_package_progress;
  UboPackageDownloadCallback deferred_package_callback;
};

class HangingInstallOperation final : public UboInstallOperation {
 public:
  HangingInstallOperation(base::FilePath package_path,
                          UboInstallCallback callback,
                          int* cancellation_count,
                          bool* terminal_callback_run)
      : package_path_(std::move(package_path)),
        callback_(std::move(callback)),
        cancellation_count_(cancellation_count),
        terminal_callback_run_(terminal_callback_run) {}

  ~HangingInstallOperation() override { Cancel(); }

  void Cancel() override {
    if (cancelled_) {
      return;
    }
    cancelled_ = true;
    ++*cancellation_count_;
    if (!package_path_.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce([](base::FilePath path) { base::DeleteFile(path); },
                         std::move(package_path_)));
    }
    if (callback_) {
      *terminal_callback_run_ = true;
      std::move(callback_).Run(
          base::unexpected(UboVerificationError::kInstallFailed));
    }
  }

 private:
  base::FilePath package_path_;
  UboInstallCallback callback_;
  raw_ptr<int> cancellation_count_;
  raw_ptr<bool> terminal_callback_run_;
  bool cancelled_ = false;
};

class UboServiceTest : public ::testing::Test {
 public:
  UboServiceTest()
      : private_key_(crypto::keypair::PrivateKey::GenerateEd25519()),
        public_key_(crypto::keypair::PublicKey::FromPrivateKey(private_key_)) {}

  void SetUp() override {
    profile_ = TestingProfile::Builder().Build();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    package_path_ = temp_dir_.GetPath().AppendASCII("ubo.crx");
    ASSERT_TRUE(base::WriteFile(package_path_, "test"));
    clock_.SetNow(base::Time::FromSecondsSinceUnixEpoch(kNowSeconds));
  }

 protected:
  UboProductConfig Config() const {
    UboProductConfig config;
    config.catalog_url = GURL("https://updates.ahoi.example/catalog.json");
    config.artifact_origin = GURL("https://updates.ahoi.example/");
    config.catalog_public_key = public_key_.ToEd25519PublicKey();
    return config;
  }

  std::string KeyHash(uint8_t byte = 0x42) const {
    std::array<uint8_t, 32> key;
    key.fill(byte);
    return base::HexEncodeLower(crypto::hash::Sha256(key));
  }

  base::DictValue Payload(uint64_t sequence = 42,
                          std::string version = "1.55.0") const {
    return base::DictValue()
        .Set("schema_version", 2)
        .Set("sequence", base::NumberToString(sequence))
        .Set("valid_from", "1999999900")
        .Set("valid_until", "2000001000")
        .Set("extension_id", kUboClassicExtensionId)
        .Set("version", std::move(version))
        .Set("package_url", "https://updates.ahoi.example/ubo.crx")
        .Set("update_manifest_url",
             "https://updates.ahoi.example/ubo-update.xml")
        .Set("sha256", kPackageHash)
        .Set("crx_public_key_sha256", KeyHash())
        .Set("upstream_tag", "1.55.0")
        .Set("upstream_commit", kUpstreamCommit)
        .Set("upstream_source_url",
             "https://github.com/gorhill/uBlock/releases/tag/1.55.0")
        .Set("license", kUboLicense);
  }

  std::string Sign(base::DictValue payload) const {
    std::string serialized = base::WriteJson(payload).value();
    std::vector<uint8_t> signature = crypto::sign::Sign(
        crypto::sign::ED25519, private_key_, base::as_byte_span(serialized));
    return base::WriteJson(base::DictValue()
                               .Set("payload", serialized)
                               .Set("signature", base::Base64Encode(signature)))
        .value();
  }

  UboCatalogEntry Entry(uint64_t sequence = 42,
                        std::string version = "1.55.0") const {
    UboCatalogEntry entry;
    entry.sequence = sequence;
    entry.extension_id = kUboClassicExtensionId;
    entry.version = base::Version(std::move(version));
    entry.package_url = GURL("https://updates.ahoi.example/ubo.crx");
    entry.update_manifest_url =
        GURL("https://updates.ahoi.example/ubo-update.xml");
    entry.package_sha256 = kPackageHash;
    entry.crx_public_key_sha256 = KeyHash();
    return entry;
  }

  scoped_refptr<const ::extensions::Extension> Extension(
      std::string version = "1.55.0") const {
    std::array<uint8_t, 32> key;
    key.fill(0x42);
    return ::extensions::ExtensionBuilder("uBlock Origin")
        .SetManifestVersion(2)
        .SetVersion(std::move(version))
        .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
        .SetID(kUboClassicExtensionId)
        .SetManifestKey("key", base::Base64Encode(key))
        .Build();
  }

  scoped_refptr<const ::extensions::Extension> PinnedBootstrapExtension()
      const {
    return ::extensions::ExtensionBuilder("uBlock Origin")
        .SetManifestVersion(2)
        .SetVersion(kUboClassicVersion)
        .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
        .SetID(kUboClassicExtensionId)
        .SetManifestKey("key", kPinnedCrxPublicKeyBase64)
        .Build();
  }

  scoped_refptr<const ::extensions::Extension> ExtensionWithId(
      std::string id,
      std::string version,
      int manifest_version) const {
    return ::extensions::ExtensionBuilder("uBlock variant")
        .SetManifestVersion(manifest_version)
        .SetVersion(std::move(version))
        .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
        .SetID(std::move(id))
        .Build();
  }

  std::unique_ptr<UboService> MakeService(
      std::unique_ptr<FakeNetworkClient> network,
      UboProductConfig config,
      UboPackageVerifier verifier = UboPackageVerifier(),
      UboInstallFunction installer = UboInstallFunction(),
      std::string process_token = "test-process") {
    if (!verifier) {
      verifier = base::BindRepeating(
          [](const UboCatalogEntry& entry, const base::FilePath&) {
            return base::expected<VerifiedUboPackage, UboVerificationError>(
                VerifiedUboPackage{
                    .extension_id = entry.extension_id,
                    .package_sha256 = entry.package_sha256,
                    .crx_public_key_sha256 = entry.crx_public_key_sha256});
          });
    }
    if (!installer) {
      installer = base::BindRepeating(
          [](Profile*, content::WebContents*, UboCatalogEntry, base::FilePath,
             UboInstallCallback callback) -> UboInstallOperationPtr {
            std::move(callback).Run(base::ok());
            return nullptr;
          });
    }
    return std::make_unique<UboService>(profile_.get(), std::move(config),
                                        std::move(network), std::move(verifier),
                                        std::move(installer), &clock_,
                                        std::move(process_token));
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_test_enabler_;
  std::unique_ptr<TestingProfile> profile_;
  base::ScopedTempDir temp_dir_;
  base::FilePath package_path_;
  base::SimpleTestClock clock_;
  crypto::keypair::PrivateKey private_key_;
  crypto::keypair::PublicKey public_key_;
};

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
