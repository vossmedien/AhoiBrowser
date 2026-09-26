// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service_unittest_support.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "crypto/hash.h"
#include "crypto/sign.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"

namespace ahoi::extensions::test_support {

FakeNetworkClient::FakeNetworkClient() = default;

FakeNetworkClient::~FakeNetworkClient() = default;

void FakeNetworkClient::FetchCatalog(const GURL& exact_url,
                                     UboCatalogDownloadCallback callback) {
  ++catalog_fetches;
  if (catalog_error) {
    std::move(callback).Run(base::unexpected(*catalog_error));
    return;
  }
  std::move(callback).Run(UboCatalogDownload{
      catalog_final_url.is_valid() ? catalog_final_url : exact_url,
      catalog_body});
}

void FakeNetworkClient::FetchPackage(const GURL& exact_url,
                                     UboDownloadProgressCallback progress,
                                     UboPackageDownloadCallback callback) {
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

void FakeNetworkClient::Cancel() {
  ++cancellations;
}

void FakeNetworkClient::CompleteDeferredPackage() {
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

HangingInstallOperation::HangingInstallOperation(base::FilePath package_path,
                                                 UboInstallCallback callback,
                                                 int* cancellation_count,
                                                 bool* terminal_callback_run)
    : package_path_(std::move(package_path)),
      callback_(std::move(callback)),
      cancellation_count_(cancellation_count),
      terminal_callback_run_(terminal_callback_run) {}

HangingInstallOperation::~HangingInstallOperation() {
  Cancel();
}

void HangingInstallOperation::Cancel() {
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

UboServiceTest::UboServiceTest()
    : private_key_(crypto::keypair::PrivateKey::GenerateEd25519()),
      public_key_(crypto::keypair::PublicKey::FromPrivateKey(private_key_)) {}

UboServiceTest::~UboServiceTest() = default;

void UboServiceTest::SetUp() {
  profile_ = TestingProfile::Builder().Build();
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  package_path_ = temp_dir_.GetPath().AppendASCII("ubo.crx");
  ASSERT_TRUE(base::WriteFile(package_path_, "test"));
  clock_.SetNow(base::Time::FromSecondsSinceUnixEpoch(kNowSeconds));
}

UboProductConfig UboServiceTest::Config() const {
  UboProductConfig config;
  config.catalog_url = GURL("https://updates.ahoi.example/catalog.json");
  config.artifact_origin = GURL("https://updates.ahoi.example/");
  config.catalog_public_key = public_key_.ToEd25519PublicKey();
  return config;
}

std::string UboServiceTest::KeyHash(uint8_t byte) const {
  std::array<uint8_t, 32> key;
  key.fill(byte);
  return base::HexEncodeLower(crypto::hash::Sha256(key));
}

base::DictValue UboServiceTest::Payload(uint64_t sequence,
                                        std::string version) const {
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

std::string UboServiceTest::Sign(base::DictValue payload) const {
  std::string serialized = base::WriteJson(payload).value();
  std::vector<uint8_t> signature = crypto::sign::Sign(
      crypto::sign::ED25519, private_key_, base::as_byte_span(serialized));
  return base::WriteJson(base::DictValue()
                             .Set("payload", serialized)
                             .Set("signature", base::Base64Encode(signature)))
      .value();
}

UboCatalogEntry UboServiceTest::Entry(uint64_t sequence,
                                      std::string version) const {
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

scoped_refptr<const ::extensions::Extension> UboServiceTest::Extension(
    std::string version) const {
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

scoped_refptr<const ::extensions::Extension>
UboServiceTest::PinnedBootstrapExtension() const {
  return ::extensions::ExtensionBuilder("uBlock Origin")
      .SetManifestVersion(2)
      .SetVersion(kUboClassicVersion)
      .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
      .SetID(kUboClassicExtensionId)
      .SetManifestKey("key", kPinnedCrxPublicKeyBase64)
      .Build();
}

scoped_refptr<const ::extensions::Extension> UboServiceTest::ExtensionWithId(
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

std::unique_ptr<UboService> UboServiceTest::MakeService(
    std::unique_ptr<FakeNetworkClient> network,
    UboProductConfig config,
    UboPackageVerifier verifier,
    UboInstallFunction installer,
    std::string process_token) {
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

}  // namespace ahoi::extensions::test_support
