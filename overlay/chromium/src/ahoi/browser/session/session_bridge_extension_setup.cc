// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/native_extension_setup_inventory.h"
#include "ahoi/browser/extensions/native_extension_setup_operation.h"
#include "ahoi/browser/session/session_bridge.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "base/functional/bind.h"
#include "base/one_shot_event.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_system.h"
#include "ui/base/base_window.h"

namespace ahoi {

void SessionBridge::InitializeNativeExtensionSetup() {
  if (auto* registrar = ::extensions::ExtensionRegistrar::Get(profile_)) {
    extension_user_settings_subscription_ =
        registrar->ObserveUserSettingsRequests(base::BindRepeating(
            &SessionBridge::OnNativeExtensionUserSettingsRequested,
            weak_ptr_factory_.GetWeakPtr()));
  }
  if (auto* system = ::extensions::ExtensionSystem::Get(profile_)) {
    system->ready().Post(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<SessionBridge> bridge) {
              if (!bridge || bridge->shutting_down_ || !bridge->profile_) {
                return;
              }
              if (auto* service =
                      sync::ProfileSyncServiceFactory::GetForProfile(
                          bridge->profile_)) {
                service->NotifyNativeExtensionSetupReady();
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
  }
}

void SessionBridge::OnNativeExtensionUserSettingsRequested(
    const ::extensions::Extension& extension,
    bool installed,
    bool enabled) {
  if (shutting_down_ || !profile_) {
    return;
  }
  const auto source =
      extensions::ReadNativeExtensionInstallSource(profile_, extension);
  if (!source) {
    return;
  }
  const auto snapshot = extensions::ReadNativeExtensionSetupInventory(profile_);
  if (!snapshot.complete ||
      std::ranges::find(snapshot.extensions, extension.id(),
                        &sync::ExtensionDesiredConfiguration::extension_id) ==
          snapshot.extensions.end()) {
    return;
  }
  auto* service = sync::ProfileSyncServiceFactory::GetForProfile(profile_);
  if (service) {
    std::ignore = service->PublishNativeExtensionUserIntent(
        {.extension_id = extension.id(),
         .source = *source,
         .installed = installed,
         .enabled = enabled});
  }
}

sync::NativeExtensionSetupSnapshot SessionBridge::ReadNativeExtensionSetup() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !profile_) {
    return {};
  }
  return extensions::ReadNativeExtensionSetupInventory(profile_);
}

void SessionBridge::ApplyNativeExtensionSetup(
    sync::ExtensionRestoreRequest request,
    base::OnceCallback<void(sync::ExtensionRestoreResult)> completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !profile_) {
    std::move(completion)
        .Run({request.operation_id, request.revision,
              sync::ExtensionRestoreDisposition::kCancelled});
    return;
  }
  gfx::NativeWindow parent_window{};
  for (const auto& [browser, state] : windows_) {
    if (browser->GetWindow() && browser->GetWindow()->IsActive()) {
      parent_window = browser->GetWindow()->GetNativeWindow();
      break;
    }
  }
  const auto extension_id = request.desired.extension_id;
  auto operation = extensions::CreateNativeExtensionSetupOperation(
      profile_, parent_window, std::move(request), std::move(completion));
  auto* pending = operation.get();
  extension_setup_operations_.insert_or_assign(extension_id,
                                               std::move(operation));
  pending->Start();
}

}  // namespace ahoi
