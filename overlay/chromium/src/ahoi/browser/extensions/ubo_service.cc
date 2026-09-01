// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service.h"

#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "ahoi/browser/extensions/ubo_simple_url_loader_client.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"

namespace ahoi::extensions {

namespace {

constexpr base::TimeDelta kPeriodicCheckInterval = base::Hours(24);

const std::string& BrowserProcessToken() {
  static const base::NoDestructor<std::string> token(
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  return *token;
}

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

UboInstallOperationPtr InstallPackage(Profile* profile,
                                      content::WebContents* web_contents,
                                      UboCatalogEntry entry,
                                      base::FilePath path,
                                      UboInstallCallback callback) {
  return InstallUboPackageFromVerifiedCatalog(profile, web_contents,
                                              std::move(entry), std::move(path),
                                              std::move(callback));
}

void DeleteTemporaryFile(base::FilePath path) {
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

void DestroyInstallOperation(UboInstallOperationPtr) {}

void EnforceDisabledBuildState(Profile* profile) {
  if (IsUboClassicEnabled() || !profile || !profile->IsRegularProfile()) {
    return;
  }

  ClearCommittedUboAuthorization(profile->GetPrefs());
  auto* registry = ::extensions::ExtensionRegistry::Get(profile);
  if (!registry || !registry->GetInstalledExtension(kUboClassicExtensionId)) {
    return;
  }
  auto* registrar = ::extensions::ExtensionRegistrar::Get(profile);
  if (registrar) {
    registrar->DisableExtension(
        kUboClassicExtensionId,
        {::extensions::disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION});
  }
}

}  // namespace

UboService::UboService(Profile* profile)
    : UboService(profile,
                 GetProductionUboProductConfig(),
                 std::make_unique<UboSimpleUrlLoaderClient>(profile),
                 base::BindRepeating(&VerifyPackage),
                 base::BindRepeating(&InstallPackage),
                 base::DefaultClock::GetInstance(),
                 BrowserProcessToken()) {
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
                       const base::Clock* clock,
                       std::string process_token)
    : profile_(profile),
      config_(std::move(config)),
      network_(std::move(network)),
      verifier_(std::move(verifier)),
      installer_(std::move(installer)),
      clock_(clock),
      process_token_(process_token.empty() ? BrowserProcessToken()
                                           : std::move(process_token)) {
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
  RefreshInventory();
  if (!config_.IsProvisioned()) {
    status_.state = UboServiceState::kUnprovisioned;
    status_.error = UboServiceError::kUnprovisioned;
    return;
  }
  status_.state = UboServiceState::kIdle;
  if (config_.IsPinnedBootstrapProvisioned()) {
    // The entry is compiled into the signed browser. Exposing it here makes
    // all trust metadata visible before the single install action without a
    // network catalog request.
    status_.catalog = GetPinnedUboBootstrapCatalogEntry();
  }
  if (RefreshInstalledState()) {
    status_.state = UboServiceState::kUpToDate;
  }
  RecomputeLiteMigrationState();
  MaybeStartPeriodicChecks();
}

UboService::~UboService() {
  Shutdown();
}

void UboService::Shutdown() {
  ++operation_generation_;
  ResetUserInstallTracking();
  weak_factory_.InvalidateWeakPtrs();
  periodic_timer_.Stop();
  if (network_) {
    network_->Cancel();
  }
  registry_observation_.Reset();
  DeletePreparedPackage();
  if (install_operation_) {
    install_operation_->Cancel();
    install_operation_.reset();
  }
  if (profile_) {
    ClearPendingUboInstallAuthorization(profile_->GetPrefs());
  }
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
         status_.state == UboServiceState::kInstalling ||
         lite_removal_in_progress_;
}

void UboService::RefreshInventory() {
  status_.inventory = UboExtensionInventory();
  if (!profile_) {
    return;
  }
  if (auto* registry = ::extensions::ExtensionRegistry::Get(profile_)) {
    status_.inventory = ReadUboExtensionInventory(*registry);
  }
}

bool UboService::RefreshInstalledState() {
  RefreshInventory();
  status_.installed_version.clear();
  status_.authorized = false;
  if (!profile_) {
    return false;
  }
  auto* registry = ::extensions::ExtensionRegistry::Get(profile_);
  if (!registry) {
    return false;
  }
  const ::extensions::Extension* installed =
      registry->GetInstalledExtension(kUboClassicExtensionId);
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

void UboService::RecomputeLiteMigrationState() {
  status_.lite_migration = UboLiteMigrationState::kNone;
  if (!profile_) {
    return;
  }
  // A failed write means there is no durable restart proof to re-read. Keep
  // that distinct failure blocked until a later explicit install succeeds;
  // never reinterpret the absent/malformed value as a neutral migration.
  if (status_.error == UboServiceError::kMigrationStateWriteFailed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    return;
  }
  if (status_.error == UboServiceError::kMigrationStateInvalid ||
      status_.error == UboServiceError::kLiteRemovalFailed) {
    status_.error = UboServiceError::kNone;
  }

  if (!profile_->GetPrefs()->FindPreference(kUboMigrationPref)) {
    return;
  }
  const base::DictValue& raw_migration =
      profile_->GetPrefs()->GetDict(kUboMigrationPref);
  std::optional<UboPersistedMigrationState> migration =
      ReadUboPersistedMigrationState(*profile_->GetPrefs());
  if (!migration) {
    if (!raw_migration.empty()) {
      status_.lite_migration = UboLiteMigrationState::kBlocked;
      status_.error = UboServiceError::kMigrationStateInvalid;
    }
    return;
  }
  std::optional<UboAuthorizationState> authorization =
      ReadCommittedUboAuthorization(*profile_->GetPrefs());
  if (!authorization ||
      !UboMigrationMatchesAuthorization(*migration, *authorization) ||
      !status_.inventory.classic.installed || !status_.authorized) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kMigrationStateInvalid;
    return;
  }
  if (lite_removal_in_progress_) {
    status_.lite_migration = UboLiteMigrationState::kRemovingLite;
    return;
  }
  if (!status_.inventory.lite.installed) {
    ClearUboPersistedMigrationState(profile_->GetPrefs());
    status_.lite_migration = UboLiteMigrationState::kComplete;
    return;
  }
  if (!status_.inventory.classic.enabled || !status_.inventory.classic.ready) {
    status_.lite_migration = UboLiteMigrationState::kClassicAwaitingReady;
    return;
  }
  status_.lite_migration = migration->install_process_token == process_token_
                               ? UboLiteMigrationState::kClassicAwaitingRestart
                               : UboLiteMigrationState::kEligibleForLiteRemoval;
}

void UboService::ResetUserInstallTracking() {
  install_web_contents_.reset();
  one_click_install_in_progress_ = false;
  status_.one_click_install_in_progress = false;
  install_handoff_waiting_for_ui_ = false;
  status_.prompt_handoff_pending = false;
  install_handed_off_ = false;
  fresh_classic_install_ = false;
}

bool UboService::IsRelevantUboIdentity(const std::string& extension_id) const {
  return extension_id == kUboClassicExtensionId ||
         extension_id == kUboFormerClassicWebStoreExtensionId ||
         extension_id == kUboLiteExtensionId;
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

void UboService::BeginPinnedBootstrapInstall(
    content::WebContents* web_contents,
    bool wait_for_install_dialog_close) {
  if (IsBusy()) {
    return;
  }
  if (!profile_ || !profile_->IsRegularProfile() ||
      !config_.IsPinnedBootstrapProvisioned() || !web_contents ||
      Profile::FromBrowserContext(web_contents->GetBrowserContext()) !=
          profile_) {
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }

  RefreshInstalledState();
  RecomputeLiteMigrationState();
  if (status_.inventory.former_classic_web_store.installed ||
      status_.inventory.classic.installed) {
    if (status_.inventory.classic.installed && status_.authorized) {
      status_.state = UboServiceState::kUpToDate;
      status_.error = UboServiceError::kNone;
      Publish();
      return;
    }
    SetError(UboServiceError::kConflictingExtension);
    return;
  }

  ++operation_generation_;
  install_web_contents_ = web_contents->GetWeakPtr();
  one_click_install_in_progress_ = true;
  status_.one_click_install_in_progress = true;
  install_handoff_waiting_for_ui_ = wait_for_install_dialog_close;
  install_handed_off_ = false;
  fresh_classic_install_ = true;
  status_.catalog = GetPinnedUboBootstrapCatalogEntry();
  status_.state = UboServiceState::kCatalogReady;
  status_.error = UboServiceError::kNone;
  PreparePackage();
}

void UboService::CancelUserInstall() {
  // Once Chromium owns the permission prompt it is the sole cancellation
  // authority. Closing the Ahoi status dialog must not dismiss or race that
  // browser-owned prompt. `install_handoff_waiting_for_ui_` only records that
  // a future prompt must wait for the Ahoi sheet to close; while download or
  // verification is still in flight the sheet remains the cancellation
  // authority.
  if (!one_click_install_in_progress_ || install_handed_off_) {
    return;
  }
  ++operation_generation_;
  ResetUserInstallTracking();
  if (network_) {
    network_->Cancel();
  }
  DeletePreparedPackage();
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  status_.state =
      status_.authorized ? UboServiceState::kUpToDate : UboServiceState::kIdle;
  status_.error = UboServiceError::kNone;
  if (config_.IsPinnedBootstrapProvisioned()) {
    status_.catalog = GetPinnedUboBootstrapCatalogEntry();
  }
  Publish();
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
  const uint64_t operation_generation = ++operation_generation_;
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
  network_->FetchCatalog(
      config_.catalog_url,
      base::BindOnce(&UboService::OnCatalogDownloaded,
                     weak_factory_.GetWeakPtr(), operation_generation, reason));
}

void UboService::OnCatalogDownloaded(
    uint64_t operation_generation,
    UboCheckReason reason,
    UboNetworkResult<UboCatalogDownload> result) {
  if (operation_generation != operation_generation_ ||
      status_.state != UboServiceState::kCheckingCatalog) {
    return;
  }
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
  const uint64_t operation_generation = ++operation_generation_;
  network_->FetchPackage(
      status_.catalog->package_url,
      base::BindRepeating(&UboService::OnPackageProgress,
                          weak_factory_.GetWeakPtr(), operation_generation),
      base::BindOnce(&UboService::OnPackageDownloaded,
                     weak_factory_.GetWeakPtr(), operation_generation));
}

void UboService::OnPackageProgress(uint64_t operation_generation,
                                   uint64_t downloaded_bytes) {
  if (operation_generation != operation_generation_ ||
      status_.state != UboServiceState::kDownloadingPackage) {
    return;
  }
  status_.downloaded_bytes = downloaded_bytes;
  Publish();
}

void UboService::OnPackageDownloaded(
    uint64_t operation_generation,
    UboNetworkResult<UboPackageDownload> result) {
  if (operation_generation != operation_generation_ ||
      status_.state != UboServiceState::kDownloadingPackage) {
    if (result.has_value() && !result->path.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&DeleteTemporaryFile, std::move(result->path)));
    }
    return;
  }
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
      base::BindOnce(&UboService::OnPackageVerified, weak_factory_.GetWeakPtr(),
                     operation_generation));
}

void UboService::OnPackageVerified(
    uint64_t operation_generation,
    base::expected<VerifiedUboPackage, UboVerificationError> result) {
  if (operation_generation != operation_generation_ ||
      status_.state != UboServiceState::kVerifyingPackage) {
    return;
  }
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
  if (!one_click_install_in_progress_) {
    return;
  }
  content::WebContents* web_contents = install_web_contents_.get();
  if (!web_contents || Profile::FromBrowserContext(
                           web_contents->GetBrowserContext()) != profile_) {
    DeletePreparedPackage();
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }
  InstallPreparedPackage(web_contents, install_handoff_waiting_for_ui_);
}

void UboService::InstallPreparedPackage(content::WebContents* web_contents,
                                        bool wait_for_install_dialog_close) {
  if (!profile_ || profile_->IsOffTheRecord() || !web_contents ||
      Profile::FromBrowserContext(web_contents->GetBrowserContext()) !=
          profile_ ||
      status_.state != UboServiceState::kPackageReady || !status_.catalog ||
      prepared_package_.empty()) {
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }
  install_web_contents_ = web_contents->GetWeakPtr();
  install_handoff_waiting_for_ui_ = wait_for_install_dialog_close;
  status_.state = UboServiceState::kInstalling;
  status_.error = UboServiceError::kNone;
  status_.prompt_handoff_pending = wait_for_install_dialog_close;
  Publish();
  if (wait_for_install_dialog_close) {
    return;
  }
  StartPreparedInstall();
}

void UboService::ContinueInstallAfterDialogClosed() {
  if (!install_handoff_waiting_for_ui_ || !status_.prompt_handoff_pending ||
      status_.state != UboServiceState::kInstalling) {
    return;
  }
  install_handoff_waiting_for_ui_ = false;
  status_.prompt_handoff_pending = false;
  Publish();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&UboService::StartPreparedInstall,
                                weak_factory_.GetWeakPtr()));
}

void UboService::StartPreparedInstall() {
  content::WebContents* web_contents = install_web_contents_.get();
  if (!profile_ || profile_->IsOffTheRecord() || !web_contents ||
      Profile::FromBrowserContext(web_contents->GetBrowserContext()) !=
          profile_ ||
      status_.state != UboServiceState::kInstalling ||
      status_.prompt_handoff_pending || !status_.catalog ||
      prepared_package_.empty() || install_operation_) {
    SetError(UboServiceError::kProfileUnavailable);
    return;
  }
  if (one_click_install_in_progress_) {
    install_handed_off_ = true;
  }
  base::FilePath package = std::move(prepared_package_);
  install_operation_ = installer_.Run(
      profile_, web_contents, *status_.catalog, std::move(package),
      base::BindOnce(&UboService::OnInstallComplete,
                     weak_factory_.GetWeakPtr()));
}

void UboService::OnInstallComplete(UboInstallResult result) {
  RetireInstallOperation();
  const bool record_fresh_migration =
      one_click_install_in_progress_ && fresh_classic_install_;
  ResetUserInstallTracking();
  if (!result.has_value()) {
    SetError(result.error() == UboVerificationError::kRollback
                 ? UboServiceError::kRollback
                 : UboServiceError::kInstallFailed);
    return;
  }
  if (!RefreshInstalledState()) {
    SetError(UboServiceError::kInstallFailed);
    return;
  }
  const std::optional<UboAuthorizationState> installed_authorization =
      ReadCommittedUboAuthorization(*profile_->GetPrefs());
  if (!status_.catalog || !installed_authorization ||
      installed_authorization->extension_id != status_.catalog->extension_id ||
      installed_authorization->version != status_.catalog->version ||
      installed_authorization->package_sha256 !=
          status_.catalog->package_sha256 ||
      installed_authorization->crx_public_key_sha256 !=
          status_.catalog->crx_public_key_sha256 ||
      status_.installed_version != status_.catalog->version.GetString()) {
    SetError(UboServiceError::kInstallFailed);
    return;
  }
  status_.state = UboServiceState::kInstalled;
  status_.error = UboServiceError::kNone;
  bool migration_write_failed = false;
  if (record_fresh_migration && status_.inventory.lite.installed) {
    if (!WriteUboPersistedMigrationState(
            profile_->GetPrefs(), *installed_authorization, process_token_)) {
      status_.lite_migration = UboLiteMigrationState::kBlocked;
      status_.error = UboServiceError::kMigrationStateWriteFailed;
      migration_write_failed = true;
    }
  }
  if (!migration_write_failed) {
    RecomputeLiteMigrationState();
  }
  MaybeStartPeriodicChecks();
  Publish();
}

void UboService::RetireInstallOperation() {
  if (!install_operation_) {
    return;
  }
  UboInstallOperationPtr operation = std::move(install_operation_);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DestroyInstallOperation, std::move(operation)));
}

void UboService::RequestRemoveUboLite() {
  if (IsBusy() || !profile_) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  if (status_.lite_migration !=
          UboLiteMigrationState::kEligibleForLiteRemoval ||
      !status_.inventory.classic.enabled || !status_.inventory.classic.ready ||
      !status_.inventory.lite.installed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
    return;
  }

  auto* registry = ::extensions::ExtensionRegistry::Get(profile_);
  auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_);
  if (!registry || !registrar ||
      !registry->GetInstalledExtension(kUboLiteExtensionId)) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
    return;
  }

  lite_removal_in_progress_ = true;
  status_.lite_migration = UboLiteMigrationState::kRemovingLite;
  status_.error = UboServiceError::kNone;
  Publish();

  std::u16string uninstall_error;
  if (!registrar->UninstallExtension(
          kUboLiteExtensionId, ::extensions::UNINSTALL_REASON_USER_INITIATED,
          &uninstall_error,
          base::BindOnce(&UboService::OnLiteUninstallCleanupComplete,
                         weak_factory_.GetWeakPtr()))) {
    lite_removal_in_progress_ = false;
    RefreshInstalledState();
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
    Publish();
  }
}

void UboService::OnLiteUninstallCleanupComplete() {
  lite_removal_in_progress_ = false;
  RefreshInstalledState();
  if (status_.inventory.lite.installed) {
    status_.lite_migration = UboLiteMigrationState::kBlocked;
    status_.error = UboServiceError::kLiteRemovalFailed;
  } else {
    ClearUboPersistedMigrationState(profile_ ? profile_->GetPrefs() : nullptr);
    status_.lite_migration = UboLiteMigrationState::kComplete;
    status_.error = UboServiceError::kNone;
    status_.state = status_.authorized ? UboServiceState::kUpToDate
                                       : UboServiceState::kInstalled;
  }
  Publish();
}

void UboService::SetError(UboServiceError error) {
  ++operation_generation_;
  ResetUserInstallTracking();
  if (network_) {
    network_->Cancel();
  }
  DeletePreparedPackage();
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
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  MaybeStartPeriodicChecks();
  Publish();
}

void UboService::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const ::extensions::Extension* extension,
    ::extensions::UninstallReason reason) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  if (extension->id() == kUboClassicExtensionId) {
    ClearCommittedUboAuthorization(profile_->GetPrefs());
    ClearUboPersistedMigrationState(profile_->GetPrefs());
    periodic_timer_.Stop();
    DeletePreparedPackage();
    status_.installed_version.clear();
    status_.authorized = false;
    status_.state = config_.IsProvisioned() ? UboServiceState::kIdle
                                            : UboServiceState::kUnprovisioned;
    status_.error = config_.IsProvisioned() ? UboServiceError::kNone
                                            : UboServiceError::kUnprovisioned;
    if (config_.IsPinnedBootstrapProvisioned()) {
      status_.catalog = GetPinnedUboBootstrapCatalogEntry();
    } else {
      status_.catalog.reset();
    }
  }
  RefreshInventory();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionLoaded(content::BrowserContext* browser_context,
                                   const ::extensions::Extension* extension) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionReady(content::BrowserContext* browser_context,
                                  const ::extensions::Extension* extension) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const ::extensions::Extension* extension,
    ::extensions::UnloadedExtensionReason reason) {
  if (browser_context != profile_ || !extension ||
      !IsRelevantUboIdentity(extension->id())) {
    return;
  }
  RefreshInstalledState();
  RecomputeLiteMigrationState();
  Publish();
}

void UboService::OnShutdown(::extensions::ExtensionRegistry* registry) {
  registry_observation_.Reset();
}

}  // namespace ahoi::extensions
