// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_install_coordinator.h"

#include <memory>
#include <optional>
#include <utility>

#include "ahoi/browser/extensions/ubo_authorization.h"
#include "ahoi/browser/extensions/ubo_package_verifier.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "components/crx_file/crx_verifier.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/crx_file_info.h"
#include "extensions/browser/crx_installer.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/install/crx_install_error.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/browser/uninstall_reason.h"

namespace ahoi::extensions {

namespace {

void DeleteTemporaryPackage(base::FilePath path) {
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

class UboInstallCoordinator final : public UboInstallOperation,
                                    public ProfileObserver {
 public:
  UboInstallCoordinator(Profile* profile,
                        content::WebContents* web_contents,
                        UboCatalogEntry entry,
                        base::FilePath package_path,
                        UboInstallCallback callback)
      : web_contents_(web_contents->GetWeakPtr()),
        entry_(std::move(entry)),
        package_path_(std::move(package_path)),
        callback_(std::move(callback)) {
    Profile* original_profile = profile->GetOriginalProfile();
    profile_ = original_profile->GetWeakPtr();
    profile_observation_.Observe(original_profile);
  }

  ~UboInstallCoordinator() override { Cancel(); }

  void Start() {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&VerifyUboPackage, entry_, package_path_),
        base::BindOnce(&UboInstallCoordinator::OnPackageVerified,
                       weak_factory_.GetWeakPtr()));
  }

  void Cancel() override {
    Finish(base::unexpected(UboVerificationError::kInstallFailed));
  }

  void OnProfileWillBeDestroyed(Profile*) override {
    profile_observation_.Reset();
    profile_.reset();
    Cancel();
  }

 private:
  void OnPackageVerified(
      base::expected<VerifiedUboPackage, UboVerificationError> package) {
    if (!package.has_value()) {
      Finish(base::unexpected(package.error()));
      return;
    }
    if (!profile_ || !web_contents_ ||
        Profile::FromBrowserContext(web_contents_->GetBrowserContext())
                ->GetOriginalProfile() != profile_.get()) {
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
      return;
    }
    auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
    auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_.get());
    if (!registry || !registrar) {
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
      return;
    }

    auto authorization = BeginUboInstallAuthorization(profile_->GetPrefs(),
                                                      entry_, package.value());
    if (!authorization.has_value()) {
      Finish(base::unexpected(authorization.error()));
      return;
    }
    authorization_ = std::move(authorization.value());

    auto prompt = std::make_unique<ExtensionInstallPrompt>(
        web_contents_.get(),
        std::make_unique<::extensions::InstallPromptData>(
            ::extensions::InstallPromptData::UNSET_PROMPT_TYPE));
    installer_ =
        ::extensions::CrxInstaller::Create(profile_.get(), std::move(prompt));
    installer_->set_expected_id(entry_.extension_id);
    installer_->set_expected_version(entry_.version,
                                     /*fail_install_if_unexpected=*/true);
    installer_->set_off_store_install_allow_reason(
        ::extensions::CrxInstaller::OffStoreInstallAllowedByVerifiedCatalog);
    // `install_immediately` controls Chromium's delayed-activation queue; it
    // does not bypass `ExtensionInstallPrompt`. Keeping silent install
    // explicitly false makes the permission-confirmation contract obvious.
    installer_->set_allow_silent_install(false);
    installer_->set_install_immediately(true);
    installer_->set_do_not_sync(true);
    // The coordinator retains and deletes the source on every terminal path.
    // CrxInstaller otherwise skips source cleanup when its BrowserContext
    // shuts down before a result callback.
    installer_->set_delete_source(false);

    const ::extensions::Extension* existing =
        registry->GetInstalledExtension(entry_.extension_id);
    installed_as_new_ = existing == nullptr;
    installer_->set_is_update(existing != nullptr);
    installer_->AddInstallerCallback(base::BindOnce(
        &UboInstallCoordinator::OnInstalled, weak_factory_.GetWeakPtr()));

    ::extensions::CRXFileInfo file_info(package_path_,
                                        crx_file::VerifierFormat::CRX3);
    file_info.extension_id = entry_.extension_id;
    file_info.expected_hash = entry_.package_sha256;
    file_info.expected_version = entry_.version;
    file_info.require_expected_hash = true;
    installer_->InstallCrxFile(file_info);
    // InstallCrxFile() cannot report failure to acquire its browser keepalive
    // through the installer callback. In that path it leaves source_file()
    // empty, so retire the operation here instead of retaining the verified
    // package and callback indefinitely.
    if (installer_ && installer_->source_file().empty()) {
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
    }
  }

  void OnInstalled(const std::optional<::extensions::CrxInstallError>& error) {
    if (error || !profile_ || !authorization_) {
      authorization_.reset();
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
      return;
    }
    auto* registry = ::extensions::ExtensionRegistry::Get(profile_.get());
    const ::extensions::Extension* installed =
        registry ? registry->GetInstalledExtension(entry_.extension_id)
                 : nullptr;
    if (!installed) {
      authorization_.reset();
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
      return;
    }
    UboInstallResult result = authorization_->Commit(*installed);
    authorization_.reset();
    if (!result.has_value()) {
      // A fresh package is not a successful install until its browser-owned
      // authorization has been durably committed. Remove it through Chromium's
      // normal uninstall path and wait for file/site-data cleanup before
      // reporting the failed transaction.
      if (installed_as_new_) {
        std::u16string uninstall_error;
        UboInstallResult cleanup_result = base::unexpected(result.error());
        auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_.get());
        if (registrar &&
            registrar->UninstallExtension(
                entry_.extension_id,
                ::extensions::UNINSTALL_REASON_INSTALL_CANCELED,
                &uninstall_error,
                base::BindOnce(
                    &UboInstallCoordinator::OnRollbackUninstallComplete,
                    weak_factory_.GetWeakPtr(), std::move(cleanup_result)))) {
          return;
        }
      }
      // A future signed-catalog update cannot be removed without also
      // removing the user's prior install. Keep it disabled if rollback could
      // not be completed; that update path remains externally gated.
      if (auto* registrar =
              ::extensions::ExtensionRegistrar::Get(profile_.get())) {
        registrar->DisableExtension(entry_.extension_id,
                                    {::extensions::disable_reason::
                                         DISABLE_UNSUPPORTED_MANIFEST_VERSION});
      }
    }
    Finish(std::move(result));
  }

  void OnRollbackUninstallComplete(UboInstallResult result) {
    Finish(std::move(result));
  }

  void Finish(UboInstallResult result) {
    if (finished_) {
      return;
    }
    finished_ = true;
    weak_factory_.InvalidateWeakPtrs();
    profile_observation_.Reset();
    authorization_.reset();
    installer_.reset();
    if (!package_path_.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&DeleteTemporaryPackage, std::move(package_path_)));
    }
    if (callback_) {
      std::move(callback_).Run(std::move(result));
    }
  }

  base::WeakPtr<Profile> profile_;
  base::WeakPtr<content::WebContents> web_contents_;
  UboCatalogEntry entry_;
  base::FilePath package_path_;
  UboInstallCallback callback_;
  std::unique_ptr<UboInstallAuthorization> authorization_;
  scoped_refptr<::extensions::CrxInstaller> installer_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  bool installed_as_new_ = false;
  bool finished_ = false;
  base::WeakPtrFactory<UboInstallCoordinator> weak_factory_{this};
};

}  // namespace

UboInstallOperationPtr InstallUboPackageFromVerifiedCatalog(
    Profile* profile,
    content::WebContents* web_contents,
    UboCatalogEntry entry,
    base::FilePath package_path,
    UboInstallCallback callback) {
  if (!profile || profile->IsOffTheRecord() || !web_contents ||
      package_path.empty() || !callback) {
    if (callback) {
      std::move(callback).Run(
          base::unexpected(UboVerificationError::kInstallFailed));
    }
    return nullptr;
  }
  auto coordinator = std::make_unique<UboInstallCoordinator>(
      profile, web_contents, std::move(entry), std::move(package_path),
      std::move(callback));
  coordinator->Start();
  return coordinator;
}

}  // namespace ahoi::extensions
