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
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/profiles/profile.h"
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

class UboInstallCoordinator : public base::RefCounted<UboInstallCoordinator> {
 public:
  UboInstallCoordinator(Profile* profile,
                        content::WebContents* web_contents,
                        UboCatalogEntry entry,
                        base::FilePath package_path,
                        UboInstallCallback callback)
      : profile_(profile->GetOriginalProfile()->GetWeakPtr()),
        web_contents_(web_contents->GetWeakPtr()),
        entry_(std::move(entry)),
        package_path_(std::move(package_path)),
        callback_(std::move(callback)) {}

  void Start() {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&VerifyUboPackage, entry_, package_path_),
        base::BindOnce(&UboInstallCoordinator::OnPackageVerified, this));
  }

 private:
  friend class base::RefCounted<UboInstallCoordinator>;
  ~UboInstallCoordinator() = default;

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
    installer_->set_install_immediately(true);
    installer_->set_do_not_sync(true);

    const ::extensions::Extension* existing =
        ::extensions::ExtensionRegistry::Get(profile_.get())
            ->GetInstalledExtension(entry_.extension_id);
    installed_as_new_ = existing == nullptr;
    installer_->set_is_update(existing != nullptr);
    installer_->AddInstallerCallback(
        base::BindOnce(&UboInstallCoordinator::OnInstalled, this));

    ::extensions::CRXFileInfo file_info(package_path_,
                                        crx_file::VerifierFormat::CRX3);
    file_info.extension_id = entry_.extension_id;
    file_info.expected_hash = entry_.package_sha256;
    file_info.expected_version = entry_.version;
    file_info.require_expected_hash = true;
    installer_->InstallCrxFile(file_info);
  }

  void OnInstalled(const std::optional<::extensions::CrxInstallError>& error) {
    if (error || !profile_ || !authorization_) {
      authorization_.reset();
      Finish(base::unexpected(UboVerificationError::kInstallFailed));
      return;
    }
    const ::extensions::Extension* installed =
        ::extensions::ExtensionRegistry::Get(profile_.get())
            ->GetInstalledExtension(entry_.extension_id);
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
        if (::extensions::ExtensionRegistrar::Get(profile_.get())
                ->UninstallExtension(
                    entry_.extension_id,
                    ::extensions::UNINSTALL_REASON_INSTALL_CANCELED,
                    &uninstall_error,
                    base::BindOnce(
                        &UboInstallCoordinator::OnRollbackUninstallComplete,
                        this, std::move(cleanup_result)))) {
          return;
        }
      }
      // A future signed-catalog update cannot be removed without also
      // removing the user's prior install. Keep it disabled if rollback could
      // not be completed; that update path remains externally gated.
      ::extensions::ExtensionRegistrar::Get(profile_.get())
          ->DisableExtension(entry_.extension_id,
                             {::extensions::disable_reason::
                                  DISABLE_UNSUPPORTED_MANIFEST_VERSION});
    }
    Finish(std::move(result));
  }

  void OnRollbackUninstallComplete(UboInstallResult result) {
    Finish(std::move(result));
  }

  void Finish(UboInstallResult result) {
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
  bool installed_as_new_ = false;
};

}  // namespace

void InstallUboPackageFromVerifiedCatalog(Profile* profile,
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
    return;
  }
  auto coordinator = base::MakeRefCounted<UboInstallCoordinator>(
      profile, web_contents, std::move(entry), std::move(package_path),
      std::move(callback));
  coordinator->Start();
}

}  // namespace ahoi::extensions
