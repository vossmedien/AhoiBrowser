// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/native_extension_setup_operation.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "ahoi/browser/extensions/native_extension_setup_inventory.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/extension_enable_flow.h"
#include "chrome/browser/ui/extensions/extension_enable_flow_delegate.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/install_approval.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/uninstall_reason.h"

namespace ahoi::extensions {
namespace {

using Disposition = sync::ExtensionRestoreDisposition;

class ScopedWebstoreInstaller : public ::extensions::WebstoreInstallWithPrompt {
 public:
  ScopedWebstoreInstaller(const std::string& id,
                          Profile* profile,
                          gfx::NativeWindow window,
                          sync::SyncAuthorization authorization,
                          bool install_disabled,
                          Callback callback)
      : WebstoreInstallWithPrompt(id, profile, window, std::move(callback)),
        authorization_(std::move(authorization)),
        install_disabled_(install_disabled) {
    set_show_post_install_ui(false);
  }

  void Cancel() {
    if (!finished_) {
      finished_ = true;
      AbortInstall();
    }
  }
  // Profile shutdown invokes the completion callback before its own cleanup,
  // without calling the virtual CompleteInstall override.
  void MarkCallbackComplete() { finished_ = true; }

 protected:
  ~ScopedWebstoreInstaller() override = default;

  void CompleteInstall(::extensions::webstore_install::Result result,
                       const std::string& error) override {
    finished_ = true;
    WebstoreInstallWithPrompt::CompleteInstall(result, error);
  }

  bool CheckRequestorAlive() const override {
    return authorization_ && authorization_.Run() &&
           WebstoreInstallWithPrompt::CheckRequestorAlive();
  }

  std::unique_ptr<::extensions::InstallApproval> CreateApproval()
      const override {
    auto approval = WebstoreInstallWithPrompt::CreateApproval();
    approval->activation_authorization = authorization_;
    approval->install_disabled = install_disabled_;
    return approval;
  }

 private:
  const sync::SyncAuthorization authorization_;
  const bool install_disabled_;
  bool finished_ = false;
};

class Operation final : public NativeExtensionSetupOperation,
                        public ExtensionEnableFlowDelegate {
 public:
  Operation(Profile* profile,
            gfx::NativeWindow window,
            sync::ExtensionRestoreRequest request,
            base::OnceCallback<void(sync::ExtensionRestoreResult)> completion)
      : profile_(profile),
        window_(window),
        request_(std::move(request)),
        completion_(std::move(completion)),
        cancelled_(std::make_shared<std::atomic<bool>>(false)),
        authorization_(base::BindRepeating(
            [](sync::SyncAuthorization original,
               std::shared_ptr<std::atomic<bool>> cancelled) {
              return !cancelled->load(std::memory_order_acquire) && original &&
                     original.Run();
            },
            request_.authorization,
            cancelled_)) {}

  ~Operation() override {
    cancelled_->store(true, std::memory_order_release);
    weak_factory_.InvalidateWeakPtrs();
    if (installer_) {
      installer_->Cancel();
    }
  }

  void Start() override {
    if (!authorization_.Run()) {
      Finish(Disposition::kCancelled);
      return;
    }
    if (!profile_ || !profile_->IsRegularProfile() ||
        profile_->IsOffTheRecord() || !request_.operation_id.is_valid() ||
        request_.revision.empty() ||
        !sync::IsValidExtensionDesiredConfiguration(request_.desired)) {
      Finish(Disposition::kUnsupported);
      return;
    }
    const auto snapshot = ReadNativeExtensionSetupInventory(profile_);
    if (!snapshot.complete) {
      Finish(Disposition::kPending);
      return;
    }
    const auto& desired = request_.desired;
    auto* registry = ::extensions::ExtensionRegistry::Get(profile_);
    auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_);
    scoped_refptr<const ::extensions::Extension> installed =
        registry->GetInstalledExtension(desired.extension_id);
    if (!installed) {
      if (!desired.installed) {
        Finish(Disposition::kApplied);
        return;
      }
      // Classic has a separate verified package/source/license consent sheet.
      // Never route its pinned MV2 identity through the Chrome Web Store.
      if (desired.source != sync::ExtensionInstallSource::kChromeWebStore) {
        Finish(Disposition::kUnsupported);
        return;
      }
      if (!request_.user_initiated || !window_) {
        Finish(Disposition::kNeedsConfirmation);
        return;
      }
      installer_ = base::MakeRefCounted<ScopedWebstoreInstaller>(
          desired.extension_id, profile_, window_, authorization_,
          !desired.enabled,
          base::BindOnce(&Operation::OnInstalled, weak_factory_.GetWeakPtr()));
      installer_->BeginInstall();
      return;
    }
    const auto source = ReadNativeExtensionInstallSource(profile_, *installed);
    if (!source || *source != desired.source) {
      Finish(Disposition::kUnsupported);
      return;
    }
    auto* policy =
        ::extensions::ExtensionSystem::Get(profile_)->management_policy();
    if (!policy->UserMayModifySettings(installed.get(), nullptr) ||
        policy->MustRemainInstalled(installed.get(), nullptr) ||
        policy->MustRemainEnabled(installed.get(), nullptr) ||
        policy->MustRemainDisabled(installed.get(), nullptr)) {
      Finish(Disposition::kBlockedByPolicy);
      return;
    }
    if (!authorization_.Run()) {
      Finish(Disposition::kCancelled);
      return;
    }
    const auto weak_this = weak_factory_.GetWeakPtr();
    if (!desired.installed) {
      // SYNC retains native policy checks; INTERNAL_MANAGEMENT would bypass
      // them.
      const bool started = registrar->UninstallExtension(
          desired.extension_id, ::extensions::UNINSTALL_REASON_SYNC, nullptr);
      if (weak_this) {
        Finish(started ? Readback() : Disposition::kFailed);
      }
      return;
    }
    if (registrar->IsExtensionEnabled(desired.extension_id) ==
        desired.enabled) {
      Finish(Readback());
      return;
    }
    if (!desired.enabled) {
      registrar->DisableExtension(
          desired.extension_id,
          {::extensions::disable_reason::DISABLE_USER_ACTION});
      if (weak_this) {
        Finish(Readback());
      }
      return;
    }
    // Passive synchronization can re-enable only an ordinary user-disabled
    // extension, never permission escalation, corruption or another disable
    // cause.
    const auto reasons =
        ::extensions::ExtensionPrefs::Get(profile_)->GetDisableReasons(
            desired.extension_id);
    const bool ordinary_disable =
        reasons.size() == 1 &&
        reasons.contains(::extensions::disable_reason::DISABLE_USER_ACTION);
    if (!request_.user_initiated) {
      if (!ordinary_disable) {
        Finish(Disposition::kNeedsConfirmation);
        return;
      }
      registrar->EnableExtension(desired.extension_id);
      if (weak_this) {
        Finish(Readback());
      }
      return;
    }
    if (!window_) {
      Finish(Disposition::kNeedsConfirmation);
      return;
    }
    enable_flow_ = std::make_unique<ExtensionEnableFlow>(
        profile_, desired.extension_id, this);
    enable_flow_->set_activation_authorization(authorization_);
    enable_flow_->StartForNativeWindow(window_);
  }

  void ExtensionEnableFlowFinished() override { Finish(Readback()); }

  void ExtensionEnableFlowAborted(bool user_initiated) override {
    Finish(authorization_.Run() ? Disposition::kNeedsConfirmation
                                : Disposition::kCancelled);
  }

 private:
  Disposition Readback() const {
    if (!authorization_.Run()) {
      return Disposition::kCancelled;
    }
    const auto snapshot = ReadNativeExtensionSetupInventory(profile_);
    if (!snapshot.complete) {
      return Disposition::kPending;
    }
    if (!request_.desired.installed) {
      return std::ranges::contains(snapshot.installed_extension_ids,
                                   request_.desired.extension_id)
                 ? Disposition::kFailed
                 : Disposition::kApplied;
    }
    return std::ranges::contains(snapshot.extensions, request_.desired)
               ? Disposition::kApplied
               : Disposition::kFailed;
  }

  void OnInstalled(bool success,
                   const std::string& error,
                   ::extensions::webstore_install::Result result) {
    installer_->MarkCallbackComplete();
    Finish(!authorization_.Run() ? Disposition::kCancelled
           : success             ? Readback()
           : result == ::extensions::webstore_install::USER_CANCELLED
               ? Disposition::kNeedsConfirmation
               : Disposition::kFailed);
  }

  void Finish(Disposition disposition) {
    if (!completion_) {
      return;
    }
    std::move(completion_)
        .Run({request_.operation_id, request_.revision, disposition});
  }

  const raw_ptr<Profile> profile_;
  const gfx::NativeWindow window_;
  const sync::ExtensionRestoreRequest request_;
  base::OnceCallback<void(sync::ExtensionRestoreResult)> completion_;
  const std::shared_ptr<std::atomic<bool>> cancelled_;
  const sync::SyncAuthorization authorization_;
  scoped_refptr<ScopedWebstoreInstaller> installer_;
  std::unique_ptr<ExtensionEnableFlow> enable_flow_;
  base::WeakPtrFactory<Operation> weak_factory_{this};
};

}  // namespace

std::unique_ptr<NativeExtensionSetupOperation>
CreateNativeExtensionSetupOperation(
    Profile* profile,
    gfx::NativeWindow parent_window,
    sync::ExtensionRestoreRequest request,
    base::OnceCallback<void(sync::ExtensionRestoreResult)> completion) {
  return std::make_unique<Operation>(profile, parent_window, std::move(request),
                                     std::move(completion));
}

}  // namespace ahoi::extensions
