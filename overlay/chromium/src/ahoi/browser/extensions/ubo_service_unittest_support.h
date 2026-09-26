// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_UNITTEST_SUPPORT_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_UNITTEST_SUPPORT_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_service.h"
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

// Fake network client, hanging install operation and the UboService
// fixture shared by the uBO service unit tests (split from
// ubo_service_unittest.cc, source line budget).
namespace ahoi::extensions::test_support {

inline constexpr int64_t kNowSeconds = 2000000000;
inline constexpr char kPackageHash[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
inline constexpr char kUpstreamCommit[] =
    "cccccccccccccccccccccccccccccccccccccccc";
inline constexpr char kPinnedCrxPublicKeyBase64[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsgdkJHEX8xHAytYy3Rih"
    "qn5FoU/cbPKhoorkCCsgF8HR2y2OSWGM1Ojrmnr0ebgM9WA1pl1hr1CmOH7DjgQ"
    "VKRhzBjK7/Zb6RVJNPVGEQvV9CdCUwOTKsu1qQGRbjm9Z/DkYxgu6B2sLo0ZpQ/"
    "IBsmBvs+FGR4CqrWra8GZPwn7n3FibeoxcArWiAx85N2Oyiaef2Geytoog4hS+I"
    "5Fs3ymKkEeTYM3tzeC0U5nZ010LCnlQe0cQ3UDOro8VzLosuhaxAsrPFErIOfIUf"
    "vV3sNhQJrySqgii9Xv6RWT8TI3pHL1yjevKKTxNb2VbPlTOi5MyzPowWV8hHJEO"
    "kwq2dQIDAQAB";

class FakeNetworkClient final : public UboNetworkClient {
 public:
  FakeNetworkClient();
  ~FakeNetworkClient() override;

  void FetchCatalog(const GURL& exact_url,
                    UboCatalogDownloadCallback callback) override;
  void FetchPackage(const GURL& exact_url,
                    UboDownloadProgressCallback progress,
                    UboPackageDownloadCallback callback) override;
  void Cancel() override;

  void CompleteDeferredPackage();

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
                          bool* terminal_callback_run);
  ~HangingInstallOperation() override;

  void Cancel() override;

 private:
  base::FilePath package_path_;
  UboInstallCallback callback_;
  raw_ptr<int> cancellation_count_;
  raw_ptr<bool> terminal_callback_run_;
  bool cancelled_ = false;
};

class UboServiceTest : public ::testing::Test {
 public:
  UboServiceTest();
  ~UboServiceTest() override;

  void SetUp() override;

 protected:
  UboProductConfig Config() const;
  std::string KeyHash(uint8_t byte = 0x42) const;
  base::DictValue Payload(uint64_t sequence = 42,
                          std::string version = "1.55.0") const;
  std::string Sign(base::DictValue payload) const;
  UboCatalogEntry Entry(uint64_t sequence = 42,
                        std::string version = "1.55.0") const;
  scoped_refptr<const ::extensions::Extension> Extension(
      std::string version = "1.55.0") const;
  scoped_refptr<const ::extensions::Extension> PinnedBootstrapExtension()
      const;
  scoped_refptr<const ::extensions::Extension> ExtensionWithId(
      std::string id,
      std::string version,
      int manifest_version) const;
  std::unique_ptr<UboService> MakeService(
      std::unique_ptr<FakeNetworkClient> network,
      UboProductConfig config,
      UboPackageVerifier verifier = UboPackageVerifier(),
      UboInstallFunction installer = UboInstallFunction(),
      std::string process_token = "test-process");

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_test_enabler_;
  std::unique_ptr<TestingProfile> profile_;
  base::ScopedTempDir temp_dir_;
  base::FilePath package_path_;
  base::SimpleTestClock clock_;
  crypto::keypair::PrivateKey private_key_;
  crypto::keypair::PublicKey public_key_;
};

}  // namespace ahoi::extensions::test_support

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_UNITTEST_SUPPORT_H_
