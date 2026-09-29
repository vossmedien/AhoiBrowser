// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/extensions/ubo_catalog.h"
#include "ahoi/browser/extensions/ubo_extension_inventory.h"
#include "ahoi/browser/extensions/ubo_install_coordinator.h"
#include "ahoi/browser/extensions/ubo_network_client.h"
#include "ahoi/browser/extensions/ubo_package_verifier.h"
#include "ahoi/browser/extensions/ubo_product_config.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "base/time/clock.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace content {
class WebContents;
}

namespace extensions {
class ExtensionRegistry;
}

namespace ahoi::extensions {

enum class UboCheckReason { kManual, kPeriodic };

enum class UboServiceState {
  kUnprovisioned,
  kIdle,
  kCheckingCatalog,
  kCatalogReady,
  kDownloadingPackage,
  kVerifyingPackage,
  kPackageReady,
  kInstalling,
  kInstalled,
  kUpToDate,
  kUpdateAvailable,
  kError,
};

enum class UboLiteMigrationState {
  kNone,
  kClassicAwaitingReady,
  kClassicAwaitingRestart,
  kEligibleForLiteRemoval,
  kRemovingLite,
  kComplete,
  kBlocked,
};

enum class UboServiceError {
  kNone,
  kUnprovisioned,
  kOffline,
  kRedirect,
  kResponseTooLarge,
  kUnexpectedResponse,
  kInvalidCatalog,
  kInvalidPackage,
  kRollback,
  kInstallFailed,
  kBusy,
  kProfileUnavailable,
  kConflictingExtension,
  kMigrationStateInvalid,
  kMigrationStateWriteFailed,
  kLiteRemovalFailed,
};

struct UboServiceStatus {
  UboServiceState state = UboServiceState::kUnprovisioned;
  UboServiceError error = UboServiceError::kNone;
  bool pinned_bootstrap_available = false;
  bool one_click_install_in_progress = false;
  std::optional<UboCatalogEntry> catalog;
  std::string installed_version;
  uint64_t downloaded_bytes = 0;
  bool authorized = false;
  // True only while the verified package is waiting for Ahoi's browser-modal
  // sheet to finish closing before Chromium creates its permission prompt.
  bool prompt_handoff_pending = false;
  UboExtensionInventory inventory;
  UboLiteMigrationState lite_migration = UboLiteMigrationState::kNone;
};

using UboPackageVerifier = base::RepeatingCallback<
    base::expected<VerifiedUboPackage, UboVerificationError>(
        const UboCatalogEntry&,
        const base::FilePath&)>;
using UboInstallFunction =
    base::RepeatingCallback<UboInstallOperationPtr(Profile*,
                                                   content::WebContents*,
                                                   UboCatalogEntry,
                                                   base::FilePath,
                                                   UboInstallCallback)>;

class UboService : public KeyedService,
                   public ::extensions::ExtensionRegistryObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnUboServiceStatusChanged(const UboServiceStatus& status) = 0;
  };

  explicit UboService(Profile* profile);
  UboService(Profile* profile,
             UboProductConfig config,
             std::unique_ptr<UboNetworkClient> network,
             UboPackageVerifier verifier,
             UboInstallFunction installer,
             const base::Clock* clock,
             std::string process_token = std::string());
  ~UboService() override;

  UboService(const UboService&) = delete;
  UboService& operator=(const UboService&) = delete;

  const UboServiceStatus& status() const { return status_; }
  bool IsPeriodicCheckEnabled() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Manual checks are always explicit. The initial pinned check is local;
  // periodic checks are signed-catalog-only and accepted solely for an
  // installed, locally authorized extension.
  void CheckForCatalog(UboCheckReason reason);
  void PreparePackage();
  void InstallPreparedPackage(content::WebContents* web_contents,
                              bool wait_for_install_dialog_close = false);

  // One explicit Ahoi action starts the statically pinned bootstrap download,
  // verification and hand-off to Chromium's normal permission prompt. It does
  // not fetch a catalog and never mutates uBO Lite.
  void BeginPinnedBootstrapInstall(content::WebContents* web_contents,
                                   bool wait_for_install_dialog_close = false);
  void ContinueInstallAfterDialogClosed();
  void CancelUserInstall();

  // This remains a distinct user gesture and is accepted only after exact
  // Classic authorization, enabled+ready runtime state, and readiness in a
  // later browser process have all been rechecked.
  void RequestRemoveUboLite();

  void RunPeriodicCheckForTesting();

  // KeyedService:
  void Shutdown() override;

  // ExtensionRegistryObserver:
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const ::extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const ::extensions::Extension* extension,
                              ::extensions::UninstallReason reason) override;
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const ::extensions::Extension* extension) override;
  void OnExtensionReady(content::BrowserContext* browser_context,
                        const ::extensions::Extension* extension) override;
  void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const ::extensions::Extension* extension,
      ::extensions::UnloadedExtensionReason reason) override;
  void OnShutdown(::extensions::ExtensionRegistry* registry) override;

 private:
  bool IsBusy() const;
  bool RefreshInstalledState();
  void RefreshInventory();
  void RecomputeLiteMigrationState();
  void ResetUserInstallTracking();
  bool IsRelevantUboIdentity(const std::string& extension_id) const;
  void MaybeStartPeriodicChecks();
  void AcceptCatalogEntry(UboCatalogEntry entry);
  void OnCatalogDownloaded(uint64_t operation_generation,
                           UboCheckReason reason,
                           UboNetworkResult<UboCatalogDownload> result);
  void OnPackageProgress(uint64_t operation_generation,
                         uint64_t downloaded_bytes);
  void OnPackageDownloaded(uint64_t operation_generation,
                           UboNetworkResult<UboPackageDownload> result);
  void OnPackageVerified(
      uint64_t operation_generation,
      base::expected<VerifiedUboPackage, UboVerificationError> result);
  void StartPreparedInstall();
  void OnInstallComplete(UboInstallResult result);
  void RetireInstallOperation();
  void OnLiteUninstallCleanupComplete();
  void SetError(UboServiceError error);
  void Publish();
  void DeletePreparedPackage();

  raw_ptr<Profile> profile_;
  UboProductConfig config_;
  std::unique_ptr<UboNetworkClient> network_;
  UboPackageVerifier verifier_;
  UboInstallFunction installer_;
  raw_ptr<const base::Clock> clock_;
  std::string process_token_;
  UboServiceStatus status_;
  base::FilePath prepared_package_;
  base::WeakPtr<content::WebContents> install_web_contents_;
  UboInstallOperationPtr install_operation_;
  bool one_click_install_in_progress_ = false;
  bool install_handoff_waiting_for_ui_ = false;
  bool install_handed_off_ = false;
  bool fresh_classic_install_ = false;
  bool lite_removal_in_progress_ = false;
  uint64_t operation_generation_ = 0;
  base::RepeatingTimer periodic_timer_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<::extensions::ExtensionRegistry,
                          ::extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::WeakPtrFactory<UboService> weak_factory_{this};
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_
