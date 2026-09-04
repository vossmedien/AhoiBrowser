// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_product_config.h"
#include "ahoi/browser/ui/extensions/ubo_install_dialog.h"
#include "ahoi/browser/ui/extensions/ubo_install_presenter.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/default_clock.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace ahoi::extensions {

namespace {

constexpr char kPinnedCrxPublicKeyBase64[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsgdkJHEX8xHAytYy3Rih"
    "qn5FoU/cbPKhoorkCCsgF8HR2y2OSWGM1Ojrmnr0ebgM9WA1pl1hr1CmOH7DjgQ"
    "VKRhzBjK7/Zb6RVJNPVGEQvV9CdCUwOTKsu1qQGRbjm9Z/DkYxgu6B2sLo0ZpQ/"
    "IBsmBvs+FGR4CqrWra8GZPwn7n3FibeoxcArWiAx85N2Oyiaef2Geytoog4hS+I"
    "5Fs3ymKkEeTYM3tzeC0U5nZ010LCnlQe0cQ3UDOro8VzLosuhaxAsrPFErIOfIUf"
    "vV3sNhQJrySqgii9Xv6RWT8TI3pHL1yjevKKTxNb2VbPlTOi5MyzPowWV8hHJEO"
    "kwq2dQIDAQAB";

class BrowserTestNetworkClient final : public UboNetworkClient {
 public:
  void FetchCatalog(const GURL&, UboCatalogDownloadCallback callback) override {
    ++catalog_fetches;
    std::move(callback).Run(
        base::unexpected(UboNetworkError::kUnexpectedResponse));
  }

  void FetchPackage(const GURL& exact_url,
                    UboDownloadProgressCallback progress,
                    UboPackageDownloadCallback callback) override {
    ++package_fetches;
    if (defer_package) {
      deferred_url = exact_url;
      deferred_progress = std::move(progress);
      deferred_callback = std::move(callback);
      return;
    }
    Complete(exact_url, std::move(progress), std::move(callback));
  }

  void Cancel() override { ++cancellations; }

  void CompleteDeferredPackage() {
    ASSERT_TRUE(deferred_callback);
    Complete(deferred_url, std::move(deferred_progress),
             std::move(deferred_callback));
  }

  base::FilePath package_path;
  GURL package_final_url;
  bool defer_package = false;
  int catalog_fetches = 0;
  int package_fetches = 0;
  int cancellations = 0;

 private:
  void Complete(const GURL& exact_url,
                UboDownloadProgressCallback progress,
                UboPackageDownloadCallback callback) {
    if (progress) {
      progress.Run(4u);
    }
    std::move(callback).Run(UboPackageDownload{
        package_final_url.is_valid() ? package_final_url : exact_url,
        package_path});
  }

  GURL deferred_url;
  UboDownloadProgressCallback deferred_progress;
  UboPackageDownloadCallback deferred_callback;
};

class UboStatusWaiter final : public UboService::Observer {
 public:
  UboStatusWaiter(UboService* service, UboServiceState target)
      : service_(service), state_target_(target) {
    service_->AddObserver(this);
  }

  UboStatusWaiter(UboService* service, UboLiteMigrationState target)
      : service_(service), migration_target_(target) {
    service_->AddObserver(this);
  }

  ~UboStatusWaiter() override { service_->RemoveObserver(this); }

  void Wait() {
    if (!Matches(service_->status())) {
      run_loop_.Run();
    }
  }

  void OnUboServiceStatusChanged(const UboServiceStatus& status) override {
    if (Matches(status)) {
      run_loop_.Quit();
    }
  }

 private:
  bool Matches(const UboServiceStatus& status) const {
    return (state_target_ && status.state == *state_target_) ||
           (migration_target_ && status.lite_migration == *migration_target_);
  }

  raw_ptr<UboService> service_;
  std::optional<UboServiceState> state_target_;
  std::optional<UboLiteMigrationState> migration_target_;
  base::RunLoop run_loop_;
};

class WidgetDestructionFlag final : public views::WidgetObserver {
 public:
  WidgetDestructionFlag(views::Widget* widget, bool* destroyed)
      : destroyed_(destroyed) {
    observation_.Observe(widget);
  }

  ~WidgetDestructionFlag() override = default;

  void OnWidgetDestroyed(views::Widget*) override {
    *destroyed_ = true;
    observation_.Reset();
  }

 private:
  raw_ptr<bool> destroyed_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

scoped_refptr<const ::extensions::Extension>
BuildExtension(std::string id, std::string version, int manifest_version) {
  return ::extensions::ExtensionBuilder("uBlock test identity")
      .SetManifestVersion(manifest_version)
      .SetVersion(std::move(version))
      .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
      .SetID(std::move(id))
      .Build();
}

scoped_refptr<const ::extensions::Extension> BuildPinnedClassic() {
  return ::extensions::ExtensionBuilder("uBlock Origin")
      .SetManifestVersion(2)
      .SetVersion(kUboClassicVersion)
      .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
      .SetID(kUboClassicExtensionId)
      .SetManifestKey("key", kPinnedCrxPublicKeyBase64)
      .Build();
}

base::expected<VerifiedUboPackage, UboVerificationError> TrustTestPackage(
    const UboCatalogEntry& entry,
    const base::FilePath&) {
  return VerifiedUboPackage{
      .extension_id = entry.extension_id,
      .package_sha256 = entry.package_sha256,
      .crx_public_key_sha256 = entry.crx_public_key_sha256,
  };
}

class UboServiceBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    package_path_ = temp_dir_.GetPath().AppendASCII("ubo.crx");
    ASSERT_TRUE(base::WriteFile(package_path_, "test"));
  }

  std::unique_ptr<UboService> MakeService(
      std::unique_ptr<BrowserTestNetworkClient> network,
      UboInstallFunction installer = UboInstallFunction(),
      std::string process_token = "browser-process-a") {
    if (!installer) {
      installer = base::BindRepeating(
          [](Profile*, content::WebContents*, UboCatalogEntry, base::FilePath,
             UboInstallCallback callback) -> UboInstallOperationPtr {
            std::move(callback).Run(
                base::unexpected(UboVerificationError::kInstallFailed));
            return nullptr;
          });
    }
    return std::make_unique<UboService>(
        browser()->GetProfile(), GetProductionUboProductConfig(),
        std::move(network), base::BindRepeating(&TrustTestPackage),
        std::move(installer), base::DefaultClock::GetInstance(),
        std::move(process_token));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath package_path_;
};

IN_PROC_BROWSER_TEST_F(UboServiceBrowserTest,
                       OneClickSkipsCatalogAndHandsOffPromptOwnership) {
  if (!IsUboClassicEnabled()) {
    GTEST_SKIP() << "uBO Classic product buildflag is disabled";
  }

  auto network = std::make_unique<BrowserTestNetworkClient>();
  BrowserTestNetworkClient* network_ptr = network.get();
  network->package_path = package_path_;
  network->package_final_url = GURL(kUboClassicPackageUrl);

  int handoffs = 0;
  content::WebContents* handed_off_web_contents = nullptr;
  UboCatalogEntry handed_off_entry;
  base::FilePath handed_off_package;
  UboInstallCallback prompt_result;
  bool dialog_destroyed = false;
  bool handoff_after_dialog_destroyed = false;
  base::RunLoop prompt_handoff;
  UboInstallFunction installer = base::BindRepeating(
      [](int* handoffs, content::WebContents** handed_off_web_contents,
         UboCatalogEntry* handed_off_entry, base::FilePath* handed_off_package,
         UboInstallCallback* prompt_result, bool* dialog_destroyed,
         bool* handoff_after_dialog_destroyed, base::RepeatingClosure quit,
         Profile*, content::WebContents* web_contents, UboCatalogEntry entry,
         base::FilePath package,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        ++*handoffs;
        *handoff_after_dialog_destroyed = *dialog_destroyed;
        *handed_off_web_contents = web_contents;
        *handed_off_entry = std::move(entry);
        *handed_off_package = std::move(package);
        *prompt_result = std::move(callback);
        quit.Run();
        return nullptr;
      },
      &handoffs, &handed_off_web_contents, &handed_off_entry,
      &handed_off_package, &prompt_result, &dialog_destroyed,
      &handoff_after_dialog_destroyed, prompt_handoff.QuitClosure());
  auto service = MakeService(std::move(network), std::move(installer));

  ASSERT_TRUE(service->status().catalog);
  const UboCatalogEntry& metadata = *service->status().catalog;
  EXPECT_TRUE(IsPinnedUboBootstrapCatalogEntry(metadata));
  EXPECT_EQ(kUboClassicExtensionId, metadata.extension_id);
  EXPECT_EQ(kUboClassicVersion, metadata.version.GetString());
  EXPECT_EQ(kUboClassicPackageSha256, metadata.package_sha256);
  EXPECT_EQ(kUboClassicCrxPublicKeySha256, metadata.crx_public_key_sha256);
  EXPECT_EQ(kUboClassicReleaseCommit, metadata.upstream_commit);
  EXPECT_EQ(kUboLicense, metadata.license);
  const UboDialogPresentation before_click =
      PresentUboStatus(service->status());
  EXPECT_TRUE(before_click.show_metadata);
  EXPECT_EQ(UboDialogAction::kBeginPinnedInstall, before_click.action);

  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active_tab);
  UboInstallDialog* dialog_ptr = nullptr;
  views::Widget* widget =
      UboInstallDialog::CreateWidget(browser(), service.get(), &dialog_ptr);
  WidgetDestructionFlag destruction_flag(widget, &dialog_destroyed);
  widget->Show();
  EXPECT_FALSE(dialog_ptr->Accept());
  prompt_handoff.Run();

  EXPECT_EQ(0, network_ptr->catalog_fetches);
  EXPECT_EQ(1, network_ptr->package_fetches);
  EXPECT_EQ(1, handoffs);
  EXPECT_EQ(active_tab, handed_off_web_contents);
  EXPECT_TRUE(IsPinnedUboBootstrapCatalogEntry(handed_off_entry));
  EXPECT_EQ(package_path_, handed_off_package);
  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  EXPECT_TRUE(dialog_destroyed);
  EXPECT_TRUE(handoff_after_dialog_destroyed);

  // The production handoff uses ExtensionInstallPrompt + CrxInstaller. Ahoi's
  // modal sheet has already closed automatically, and its window-closing
  // callback has committed ownership of the browser-owned prompt.
  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  ASSERT_TRUE(prompt_result);
  std::move(prompt_result)
      .Run(base::unexpected(UboVerificationError::kInstallFailed));
  EXPECT_FALSE(
      ReadCommittedUboAuthorization(*browser()->GetProfile()->GetPrefs()));
  {
    const base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_TRUE(base::DeleteFile(handed_off_package));
  }
}

IN_PROC_BROWSER_TEST_F(UboServiceBrowserTest,
                       ExplicitClickSeedsPromptHostFromZeroTabs) {
  if (!IsUboClassicEnabled()) {
    GTEST_SKIP() << "uBO Classic product buildflag is disabled";
  }

  browser()->tab_strip_model()->DetachAndDeleteWebContentsAt(0);
  ASSERT_EQ(0, browser()->tab_strip_model()->count());
  ASSERT_FALSE(browser()->tab_strip_model()->GetActiveWebContents());

  auto network = std::make_unique<BrowserTestNetworkClient>();
  network->package_path = package_path_;
  network->package_final_url = GURL(kUboClassicPackageUrl);

  content::WebContents* handed_off_web_contents = nullptr;
  base::FilePath handed_off_package;
  UboInstallCallback prompt_result;
  bool dialog_destroyed = false;
  bool handoff_after_dialog_destroyed = false;
  base::RunLoop prompt_handoff;
  UboInstallFunction installer = base::BindRepeating(
      [](content::WebContents** handed_off_web_contents,
         base::FilePath* handed_off_package, UboInstallCallback* prompt_result,
         bool* dialog_destroyed, bool* handoff_after_dialog_destroyed,
         base::RepeatingClosure quit, Profile*,
         content::WebContents* web_contents, UboCatalogEntry,
         base::FilePath package,
         UboInstallCallback callback) -> UboInstallOperationPtr {
        *handoff_after_dialog_destroyed = *dialog_destroyed;
        *handed_off_web_contents = web_contents;
        *handed_off_package = std::move(package);
        *prompt_result = std::move(callback);
        quit.Run();
        return nullptr;
      },
      &handed_off_web_contents, &handed_off_package, &prompt_result,
      &dialog_destroyed, &handoff_after_dialog_destroyed,
      prompt_handoff.QuitClosure());
  auto service = MakeService(std::move(network), std::move(installer));
  UboInstallDialog* dialog_ptr = nullptr;
  views::Widget* widget =
      UboInstallDialog::CreateWidget(browser(), service.get(), &dialog_ptr);
  WidgetDestructionFlag destruction_flag(widget, &dialog_destroyed);
  widget->Show();

  // The explicit CTA creates only a standard foreground tab to host the
  // normal Chromium permission prompt. Nothing is installed silently.
  EXPECT_FALSE(dialog_ptr->Accept());
  prompt_handoff.Run();

  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(),
            handed_off_web_contents);
  EXPECT_EQ(UboServiceState::kInstalling, service->status().state);
  EXPECT_TRUE(dialog_destroyed);
  EXPECT_TRUE(handoff_after_dialog_destroyed);
  ASSERT_TRUE(prompt_result);
  std::move(prompt_result)
      .Run(base::unexpected(UboVerificationError::kInstallFailed));
  EXPECT_FALSE(
      ReadCommittedUboAuthorization(*browser()->GetProfile()->GetPrefs()));
  EXPECT_FALSE(
      ReadUboPersistedMigrationState(*browser()->GetProfile()->GetPrefs()));
  {
    const base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_TRUE(base::DeleteFile(handed_off_package));
  }
}

IN_PROC_BROWSER_TEST_F(UboServiceBrowserTest,
                       LostTabBeforePromptDeletesPackageAndAuthorization) {
  if (!IsUboClassicEnabled()) {
    GTEST_SKIP() << "uBO Classic product buildflag is disabled";
  }

  auto network = std::make_unique<BrowserTestNetworkClient>();
  BrowserTestNetworkClient* network_ptr = network.get();
  network->defer_package = true;
  network->package_path = package_path_;
  auto service = MakeService(std::move(network));
  auto transient_tab = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));

  service->BeginPinnedBootstrapInstall(transient_tab.get());
  ASSERT_EQ(UboServiceState::kDownloadingPackage, service->status().state);
  UboStatusWaiter failed(service.get(), UboServiceState::kError);
  transient_tab.reset();
  network_ptr->CompleteDeferredPackage();
  failed.Wait();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(UboServiceError::kProfileUnavailable, service->status().error);
  {
    const base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_FALSE(base::PathExists(package_path_));
  }
  EXPECT_FALSE(
      ReadCommittedUboAuthorization(*browser()->GetProfile()->GetPrefs()));
}

IN_PROC_BROWSER_TEST_F(UboServiceBrowserTest,
                       InventoriesAllThreeExactIdsIndependently) {
  if (!IsUboClassicEnabled()) {
    GTEST_SKIP() << "uBO Classic product buildflag is disabled";
  }

  auto* registry =
      ::extensions::ExtensionRegistry::Get(browser()->GetProfile());
  ASSERT_TRUE(registry);
  auto classic = BuildPinnedClassic();
  auto former = BuildExtension(kUboFormerClassicWebStoreExtensionId, "1.60.0",
                               /*manifest_version=*/2);
  auto lite = BuildExtension(kUboLiteExtensionId, "2026.8.31.0",
                             /*manifest_version=*/3);
  ASSERT_TRUE(registry->AddEnabled(classic));
  ASSERT_TRUE(registry->AddReady(classic));
  ASSERT_TRUE(registry->AddDisabled(former));
  ASSERT_TRUE(registry->AddEnabled(lite));
  ASSERT_TRUE(registry->AddReady(lite));

  auto service = MakeService(std::make_unique<BrowserTestNetworkClient>());
  const UboExtensionInventory& inventory = service->status().inventory;
  EXPECT_TRUE(inventory.classic.installed);
  EXPECT_TRUE(inventory.classic.enabled);
  EXPECT_TRUE(inventory.classic.ready);
  EXPECT_EQ(kUboClassicVersion, inventory.classic.version);
  EXPECT_TRUE(inventory.former_classic_web_store.installed);
  EXPECT_FALSE(inventory.former_classic_web_store.enabled);
  EXPECT_EQ("1.60.0", inventory.former_classic_web_store.version);
  EXPECT_TRUE(inventory.lite.installed);
  EXPECT_TRUE(inventory.lite.enabled);
  EXPECT_TRUE(inventory.lite.ready);
  EXPECT_EQ("2026.8.31.0", inventory.lite.version);
}

IN_PROC_BROWSER_TEST_F(
    UboServiceBrowserTest,
    LiteRemovalNeedsReadyAuthorizedClassicAndLaterProcessToken) {
  if (!IsUboClassicEnabled()) {
    GTEST_SKIP() << "uBO Classic product buildflag is disabled";
  }

  Profile* profile = browser()->GetProfile();
  auto classic = BuildPinnedClassic();
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  auto authorization = BeginUboInstallAuthorization(
      profile->GetPrefs(), entry,
      VerifiedUboPackage{
          .extension_id = entry.extension_id,
          .package_sha256 = entry.package_sha256,
          .crx_public_key_sha256 = entry.crx_public_key_sha256,
      });
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE((*authorization)->Commit(*classic).has_value());

  auto* registrar = ::extensions::ExtensionRegistrar::Get(profile);
  auto* registry = ::extensions::ExtensionRegistry::Get(profile);
  ASSERT_TRUE(registrar);
  ASSERT_TRUE(registry);
  registrar->AddExtension(classic);
  auto lite = BuildExtension(kUboLiteExtensionId, "2026.8.31.0",
                             /*manifest_version=*/3);
  registrar->AddExtension(lite);
  ASSERT_TRUE(registry->enabled_extensions().Contains(kUboClassicExtensionId));
  ASSERT_TRUE(registry->ready_extensions().Contains(kUboClassicExtensionId));
  ASSERT_TRUE(registry->enabled_extensions().Contains(kUboLiteExtensionId));
  ASSERT_TRUE(WriteUboPersistedMigrationState(
      profile->GetPrefs(), *ReadCommittedUboAuthorization(*profile->GetPrefs()),
      "process-a"));

  {
    auto install_process =
        MakeService(std::make_unique<BrowserTestNetworkClient>(),
                    UboInstallFunction(), "process-a");
    EXPECT_EQ(UboLiteMigrationState::kClassicAwaitingRestart,
              install_process->status().lite_migration);
    install_process->RequestRemoveUboLite();
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }

  {
    // Recreating a profile service is not restart proof. Every service in the
    // same browser process receives the same process-wide token.
    auto same_browser_process =
        MakeService(std::make_unique<BrowserTestNetworkClient>(),
                    UboInstallFunction(), "process-a");
    EXPECT_EQ(UboLiteMigrationState::kClassicAwaitingRestart,
              same_browser_process->status().lite_migration);
    EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));
  }

  auto later_process = MakeService(std::make_unique<BrowserTestNetworkClient>(),
                                   UboInstallFunction(), "process-b");
  ASSERT_EQ(UboLiteMigrationState::kEligibleForLiteRemoval,
            later_process->status().lite_migration);
  EXPECT_TRUE(registry->GetInstalledExtension(kUboLiteExtensionId));

  UboStatusWaiter removed(later_process.get(),
                          UboLiteMigrationState::kComplete);
  later_process->RequestRemoveUboLite();
  removed.Wait();

  EXPECT_FALSE(registry->GetInstalledExtension(kUboLiteExtensionId));
  EXPECT_FALSE(ReadUboPersistedMigrationState(*profile->GetPrefs()));
  EXPECT_TRUE(registry->GetInstalledExtension(kUboClassicExtensionId));
}

}  // namespace

}  // namespace ahoi::extensions
