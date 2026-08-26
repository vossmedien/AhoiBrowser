// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/extensions/ubo_catalog.h"
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
};

struct UboServiceStatus {
  UboServiceState state = UboServiceState::kUnprovisioned;
  UboServiceError error = UboServiceError::kNone;
  std::optional<UboCatalogEntry> catalog;
  std::string installed_version;
  uint64_t downloaded_bytes = 0;
  bool authorized = false;
};

using UboPackageVerifier = base::RepeatingCallback<
    base::expected<VerifiedUboPackage, UboVerificationError>(
        const UboCatalogEntry&,
        const base::FilePath&)>;
using UboInstallFunction = base::RepeatingCallback<void(Profile*,
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
             const base::Clock* clock);
  ~UboService() override;

  UboService(const UboService&) = delete;
  UboService& operator=(const UboService&) = delete;

  const UboServiceStatus& status() const { return status_; }
  bool IsPeriodicCheckEnabled() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Manual checks are always explicit. Periodic checks are catalog-only and
  // accepted solely for an installed, locally authorized extension.
  void CheckForCatalog(UboCheckReason reason);
  void PreparePackage();
  void InstallPreparedPackage(content::WebContents* web_contents);

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
  void OnShutdown(::extensions::ExtensionRegistry* registry) override;

 private:
  bool IsBusy() const;
  bool RefreshInstalledState();
  void MaybeStartPeriodicChecks();
  void OnCatalogDownloaded(UboCheckReason reason,
                           UboNetworkResult<UboCatalogDownload> result);
  void OnPackageProgress(uint64_t downloaded_bytes);
  void OnPackageDownloaded(UboNetworkResult<UboPackageDownload> result);
  void OnPackageVerified(
      base::expected<VerifiedUboPackage, UboVerificationError> result);
  void OnInstallComplete(UboInstallResult result);
  void SetError(UboServiceError error);
  void Publish();
  void DeletePreparedPackage();

  raw_ptr<Profile> profile_;
  UboProductConfig config_;
  std::unique_ptr<UboNetworkClient> network_;
  UboPackageVerifier verifier_;
  UboInstallFunction installer_;
  raw_ptr<const base::Clock> clock_;
  UboServiceStatus status_;
  base::FilePath prepared_package_;
  base::RepeatingTimer periodic_timer_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<::extensions::ExtensionRegistry,
                          ::extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::WeakPtrFactory<UboService> weak_factory_{this};
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_H_
