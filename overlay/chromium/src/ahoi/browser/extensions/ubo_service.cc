// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service.h"

#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_simple_url_loader_client.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"

namespace ahoi::extensions {

namespace {

constexpr base::TimeDelta kPeriodicCheckInterval = base::Hours(24);

UboServiceError NetworkErrorToServiceError(UboNetworkError error) {
  switch (error) {
    case UboNetworkError::kOffline:
      return UboServiceError::kOffline;
    case UboNetworkError::kRedirect:
      return UboServiceError::kRedirect;
    case UboNetworkError::kResponseTooLarge:
      return UboServiceError::kResponseTooLarge;
    case UboNetworkError::kInvalidRequest:
      return UboServiceError::kProfileUnavailable;
    case UboNetworkError::kUnexpectedResponse:
    case UboNetworkError::kCancelled:
      return UboServiceError::kUnexpectedResponse;
  }
}

base::expected<VerifiedUboPackage, UboVerificationError> VerifyPackage(
    const UboCatalogEntry& entry,
    const base::FilePath& path) {
  return VerifyUboPackage(entry, path);
}

void InstallPackage(Profile* profile,
                    content::WebContents* web_contents,
                    UboCatalogEntry entry,
                    base::FilePath path,
                    UboInstallCallback callback) {
  InstallUboPackageFromVerifiedCatalog(profile, web_contents, std::move(entry),
                                       std::move(path), std::move(callback));
}

void DeleteTemporaryFile(base::FilePath path) {
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

void EnforceDisabledBuildState(Profile* profile) {
  if (IsUboClassicEnabled() || !profile || !profile->IsRegularProfile()) {
    return;
  }

  ClearCommittedUboAuthorization(profile->GetPrefs());
  auto* registry = ::extensions::ExtensionRegistry::Get(profile);
  if (!registry ||
      !registry->GetInstalledExtension(kUboClassicExtensionId)) {
    return;
  }
  ::extensions::ExtensionRegistrar::Get(profile)->DisableExtension(
      kUboClassicExtensionId,
      {::extensions::disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION});
}

}  // namespace

UboService::UboService(Profile* profile)
    : UboService(profile,
                 GetProductionUboProductConfig(),
                 std::make_unique<UboSimpleUrlLoaderClient>(profile),
                 base::BindRepeating(&VerifyPackage),
                 base::BindRepeating(&InstallPackage),
                 base::DefaultClock::GetInstance()) {
  // The service is created eagerly for regular profiles so a build with the
  // gate disabled can revoke stale local authorization and disable the exact
  // MV2 extension before any product surface becomes reachable.
  EnforceDisabledBuildState(profile);
}

UboService::UboService(Profile* profile,
                       UboProductConfig config,
                       std::unique_ptr<UboNetworkClient> network,
                       UboPackageVerifier verifier,
                       UboInstallFunction installer,
                       const base::Clock* clock)
    : profile_(profile),
      config_(std::move(config)),
      network_(std::move(network)),
      verifier_(std::move(verifier)),
      installer_(std::move(installer)),
      clock_(clock) {
  status_.pinned_bootstrap_available = config_.IsPinnedBootstrapProvisioned();
  if (!profile_ || !profile_->IsRegularProfile() || !network_ || !verifier_ ||
      !installer_ || !clock_) {
    status_.state = UboServiceState::kError;
    status_.error = UboServiceError::kProfileUnavailable;
    return;
  }
  if (auto* registry = ::extensions::ExtensionRegistry::Get(profile_)) {
    registry_observation_.Observe(registry);
  }
  if (!config_.IsProvisioned()) {
    status_.state = UboServiceState::kUnprovisioned;
    status_.error = UboServiceError::kUnprovisioned;
    return;
  }
  status_.state = UboServiceState::kIdle;
  RefreshInstalledState();
  MaybeStartPeriodicChecks();
}

UboService::~UboService() {
  Shutdown();
}

void UboService::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
  periodic_timer_.Stop();
  if (network_) {
    network_->Cancel();
  }
  registry_observation_.Reset();
  DeletePreparedPackage();
  profile_ = nullptr;
}

void UboService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void UboService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool UboService::IsBusy() const {
  return status_.state == UboServiceState::kCheckingCatalog ||
         status_.state == UboServiceState::kDownloadingPackage ||
         status_.state == UboServiceState::kVerifyingPackage ||
         status_.state == UboServiceState::kInstalling;
}

bool UboService::RefreshInstalledState() {
  status_.installed_version.clear();
  status_.authorized = false;
  if (!profile_) {
    return false;
  }
  const ::extensions::Extension* installed =
      ::extensions::ExtensionRegistry::Get(profile_)->GetInstalledExtension(
          kUboClassicExtensionId);
  if (!installed) {
    return false;
  }
  status_.installed_version = installed->version().GetString();
  // A pending install transaction may temporarily satisfy the runtime MV2
  // predicate. Periodic network activity requires the stronger condition: a
  // successfully committed authorization matching the installed extension.
  status_.authorized =
      ReadCommittedUboAuthorization(*profile_->GetPrefs()).has_value() &&
      IsUboManifestV2ExtensionAllowed(*profile_->GetPrefs(), *installed);
  return status_.authorized;
}

bool UboService::IsPeriodicCheckEnabled() const {
  return profile_ && config_.IsSignedCatalogProvisioned() &&
         status_.authorized && !status_.installed_version.empty();
}

void UboService::MaybeStartPeriodicChecks() {
  if (!IsPeriodicCheckEnabled()) {
    periodic_timer_.Stop();
    return;
  }
  if (!periodic_timer_.IsRunning()) {
    periodic_timer_.Start(FROM_HERE, kPeriodicCheckInterval, this,
                          &UboService::RunPeriodicCheckForTesting);
  }
}

void UboService::RunPeriodicCheckForTesting() {
  CheckForCatalog(UboCheckReason::kPeriodic);
}

void UboService::CheckForCatalog(UboCheckReason reason) {
  if (!profile_ || !profile_->IsRegularProfile()) {
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }
  if (!config_.IsProvisioned()) {
    status_.state = UboServiceState::kUnprovisioned;
    status_.error = UboServiceError::kUnprovisioned;
    Publish();
    return;
  }
  if (IsBusy()) {
    return;
  }
  if (reason == UboCheckReason::kPeriodic && !IsPeriodicCheckEnabled()) {
    return;
  }

  DeletePreparedPackage();
  status_.catalog.reset();
  status_.downloaded_bytes = 0;
  status_.error = UboServiceError::kNone;
  const bool installed_and_authorized = RefreshInstalledState();
  if (reason == UboCheckReason::kManual &&
      config_.IsPinnedBootstrapProvisioned() &&
      (!installed_and_authorized || !config_.IsSignedCatalogProvisioned())) {
    AcceptCatalogEntry(GetPinnedUboBootstrapCatalogEntry());
    return;
  }
  // The network catalog is update-only. It must never become an alternate
  // first-install authority when the statically pinned bootstrap is absent.
  if (!config_.IsSignedCatalogProvisioned() || !installed_and_authorized) {
    status_.state = UboServiceState::kUnprovisioned;
    status_.error = UboServiceError::kUnprovisioned;
    Publish();
    return;
  }

  status_.state = UboServiceState::kCheckingCatalog;
  Publish();
  network_->FetchCatalog(config_.catalog_url,
                         base::BindOnce(&UboService::OnCatalogDownloaded,
                                        weak_factory_.GetWeakPtr(), reason));
}

void UboService::OnCatalogDownloaded(
    UboCheckReason reason,
    UboNetworkResult<UboCatalogDownload> result) {
  if (!result.has_value()) {
    SetError(NetworkErrorToServiceError(result.error()));
    return;
  }
  auto verified =
      VerifyUboCatalog(config_, result->final_url, result->body, clock_->Now());
  if (!verified.has_value()) {
    SetError(verified.error() == UboVerificationError::kRollback
                 ? UboServiceError::kRollback
                 : UboServiceError::kInvalidCatalog);
    return;
  }
  AcceptCatalogEntry(std::move(verified.value()));
}

void UboService::AcceptCatalogEntry(UboCatalogEntry entry) {
  const bool pinned_bootstrap = IsPinnedUboBootstrapCatalogEntry(entry);
  const bool installed_and_authorized = RefreshInstalledState();
  // Recheck after the asynchronous catalog fetch. Uninstall or authorization
  // removal while the request is in flight must not turn an update candidate
  // into a new-install candidate.
  if (!pinned_bootstrap && !installed_and_authorized) {
    status_.state = UboServiceState::kUnprovisioned;
    status_.error = UboServiceError::kUnprovisioned;
    Publish();
    return;
  }
  auto rollback = CheckUboCatalogAgainstCommittedAuthorization(
      *profile_->GetPrefs(), entry);
  if (!rollback.has_value()) {
    SetError(UboServiceError::kRollback);
    return;
  }

  status_.catalog = std::move(entry);
  status_.error = UboServiceError::kNone;
  if (installed_and_authorized) {
    base::Version installed(status_.installed_version);
    status_.state = installed.IsValid() && installed >= status_.catalog->version
                        ? UboServiceState::kUpToDate
                        : UboServiceState::kUpdateAvailable;
  } else {
    status_.state = UboServiceState::kCatalogReady;
  }
  // A periodic result remains purely informational. Package download and the
  // Chromium permission prompt require later, explicit user interaction.
  Publish();
}

void UboService::PreparePackage() {
  if (IsBusy()) {
    return;
  }
  if (!status_.catalog ||
      (status_.state != UboServiceState::kCatalogReady &&
       status_.state != UboServiceState::kUpdateAvailable)) {
    SetError(UboServiceError::kInvalidCatalog);
    return;
  }
  DeletePreparedPackage();
  status_.downloaded_bytes = 0;
  status_.error = UboServiceError::kNone;
  status_.state = UboServiceState::kDownloadingPackage;
  Publish();
  network_->FetchPackage(status_.catalog->package_url,
                         base::BindRepeating(&UboService::OnPackageProgress,
                                             weak_factory_.GetWeakPtr()),
                         base::BindOnce(&UboService::OnPackageDownloaded,
                                        weak_factory_.GetWeakPtr()));
}

void UboService::OnPackageProgress(uint64_t downloaded_bytes) {
  if (status_.state != UboServiceState::kDownloadingPackage) {
    return;
  }
  status_.downloaded_bytes = downloaded_bytes;
  Publish();
}

void UboService::OnPackageDownloaded(
    UboNetworkResult<UboPackageDownload> result) {
  if (!result.has_value()) {
    SetError(NetworkErrorToServiceError(result.error()));
    return;
  }
  if (!status_.catalog ||
      !IsAllowedUboPackageFinalUrl(status_.catalog->package_url,
                                   result->final_url) ||
      result->path.empty()) {
    if (!result->path.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&DeleteTemporaryFile, std::move(result->path)));
    }
    SetError(UboServiceError::kInvalidPackage);
    return;
  }
  prepared_package_ = std::move(result->path);
  status_.state = UboServiceState::kVerifyingPackage;
  Publish();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(verifier_, *status_.catalog, prepared_package_),
      base::BindOnce(&UboService::OnPackageVerified,
                     weak_factory_.GetWeakPtr()));
}

void UboService::OnPackageVerified(
    base::expected<VerifiedUboPackage, UboVerificationError> result) {
  if (!result.has_value() || !status_.catalog ||
      result->extension_id != status_.catalog->extension_id ||
      result->package_sha256 != status_.catalog->package_sha256 ||
      result->crx_public_key_sha256 != status_.catalog->crx_public_key_sha256) {
    DeletePreparedPackage();
    SetError(UboServiceError::kInvalidPackage);
    return;
  }
  status_.state = UboServiceState::kPackageReady;
  status_.error = UboServiceError::kNone;
  Publish();
}

void UboService::InstallPreparedPackage(content::WebContents* web_contents) {
  if (!profile_ || profile_->IsOffTheRecord() || !web_contents ||
      Profile::FromBrowserContext(web_contents->GetBrowserContext()) !=
          profile_ ||
      status_.state != UboServiceState::kPackageReady || !status_.catalog ||
      prepared_package_.empty()) {
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }
  status_.state = UboServiceState::kInstalling;
  status_.error = UboServiceError::kNone;
  Publish();
  base::FilePath package = std::move(prepared_package_);
  installer_.Run(profile_, web_contents, *status_.catalog, std::move(package),
                 base::BindOnce(&UboService::OnInstallComplete,
                                weak_factory_.GetWeakPtr()));
}

void UboService::OnInstallComplete(UboInstallResult result) {
  if (!result.has_value()) {
    SetError(result.error() == UboVerificationError::kRollback
                 ? UboServiceError::kRollback
                 : UboServiceError::kInstallFailed);
    return;
  }
  RefreshInstalledState();
  status_.state = UboServiceState::kInstalled;
  status_.error = UboServiceError::kNone;
  MaybeStartPeriodicChecks();
  Publish();
}

void UboService::SetError(UboServiceError error) {
  status_.state = UboServiceState::kError;
  status_.error = error;
  Publish();
}

void UboService::Publish() {
  for (Observer& observer : observers_) {
    observer.OnUboServiceStatusChanged(status_);
  }
}

void UboService::DeletePreparedPackage() {
  if (prepared_package_.empty()) {
    return;
  }
  base::FilePath path = std::move(prepared_package_);
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&DeleteTemporaryFile, std::move(path)));
}

void UboService::OnExtensionInstalled(content::BrowserContext* browser_context,
                                      const ::extensions::Extension* extension,
                                      bool is_update) {
  if (browser_context != profile_ || !extension ||
      extension->id() != kUboClassicExtensionId) {
    return;
  }
  RefreshInstalledState();
  MaybeStartPeriodicChecks();
  Publish();
}

void UboService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const ::extensions::Extension* extension,
    ::extensions::UninstallReason reason) {
  if (browser_context != profile_ || !extension ||
      extension->id() != kUboClassicExtensionId) {
    return;
  }
  ClearCommittedUboAuthorization(profile_->GetPrefs());
  periodic_timer_.Stop();
  DeletePreparedPackage();
  status_.catalog.reset();
  status_.installed_version.clear();
  status_.authorized = false;
  status_.state = config_.IsProvisioned() ? UboServiceState::kIdle
                                          : UboServiceState::kUnprovisioned;
  status_.error = config_.IsProvisioned() ? UboServiceError::kNone
                                          : UboServiceError::kUnprovisioned;
  Publish();
}

void UboService::OnShutdown(::extensions::ExtensionRegistry* registry) {
  registry_observation_.Reset();
}

}  // namespace ahoi::extensions
