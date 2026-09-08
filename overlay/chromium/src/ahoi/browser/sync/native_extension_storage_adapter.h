// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_ADAPTER_H_
#define AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_ADAPTER_H_

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/extension_storage_types.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_management.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_prefs_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace ahoi::sync {

struct NativeExtensionStorageCommit;

// Profile-owned consumer of Chromium's actual storage.sync. No Browser,
// SessionBridge, JS injection, second ValueStore, or extension installation.
class NativeExtensionStorageAdapter final
    : public extensions::ExtensionRegistryObserver,
      public extensions::ExtensionPrefsObserver,
      public extensions::ExtensionManagement::Observer {
 public:
  using LocalChange = base::RepeatingCallback<void(ExtensionStorageValue)>;
  NativeExtensionStorageAdapter(Profile* profile,
                                LocalChange changed,
                                base::RepeatingClosure ready);
  ~NativeExtensionStorageAdapter() override;
  base::WeakPtr<NativeExtensionStorageAdapter> GetWeakPtr();
  void Read(
      SyncAuthorization authorization,
      base::OnceCallback<void(NativeExtensionStorageSnapshot)> completion);
  void Apply(ExtensionStorageRequest request,
             base::OnceCallback<void(ExtensionStorageResult)> completion);

 private:
  struct ReadBatch;
  scoped_refptr<const extensions::Extension> Eligible(
      std::string_view id,
      ExtensionStorageDisposition* failure = nullptr) const;
  SyncAuthorization CaptureScope(const std::string& id,
                                 SyncAuthorization original);
  void Invalidate(const std::string& id);
  void InvalidateAndRefresh(const std::string& id);
  void InvalidateAll();
  void Stop();
  void Ready();
  void OnNativeWriteRequested(const extensions::ExtensionId& id);
  void OnChanged(const extensions::ExtensionId& id,
                 const base::DictValue& changes,
                 extensions::StorageFrontend::SyncSettingsChangeOrigin origin);
  void OnRead(std::shared_ptr<ReadBatch> batch,
              scoped_refptr<const extensions::Extension> expected,
              SyncAuthorization authorization,
              NativeExtensionStorageSnapshot snapshot);
  void AfterApplyRead(
      ExtensionStorageRequest request,
      scoped_refptr<const extensions::Extension> expected,
      base::OnceCallback<void(ExtensionStorageResult)> completion,
      NativeExtensionStorageSnapshot snapshot);
  void OnWritten(ExtensionStorageRequest request,
                 scoped_refptr<const extensions::Extension> expected,
                 extensions::SettingsChangedCallback notify,
                 base::OnceCallback<void(ExtensionStorageResult)> completion,
                 NativeExtensionStorageCommit committed);
  void OnExtensionManagementSettingsChanged() override;
  void OnExtensionLoaded(content::BrowserContext*,
                         const extensions::Extension*) override;
  void OnExtensionUnloaded(content::BrowserContext*,
                           const extensions::Extension*,
                           extensions::UnloadedExtensionReason) override;
  void OnExtensionInstalled(content::BrowserContext*,
                            const extensions::Extension*,
                            bool) override;
  void OnExtensionUninstalled(content::BrowserContext*,
                              const extensions::Extension*,
                              extensions::UninstallReason) override;
  void OnShutdown(extensions::ExtensionRegistry*) override;
  void OnExtensionPrefsUpdated(const extensions::ExtensionId& id) override;
  void OnExtensionRuntimePermissionsChanged(
      const extensions::ExtensionId& id) override;
  void OnExtensionPrefsWillBeDestroyed(extensions::ExtensionPrefs*) override;

  raw_ptr<Profile> profile_;
  raw_ptr<extensions::StorageFrontend> frontend_ = nullptr;
  const LocalChange changed_;
  const base::RepeatingClosure ready_;
  std::shared_ptr<std::atomic<bool>> stopped_ =
      std::make_shared<std::atomic<bool>>(false);
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> epochs_;
  base::CallbackListSubscription changes_subscription_;
  base::CallbackListSubscription requests_subscription_;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::ScopedObservation<extensions::ExtensionPrefs,
                          extensions::ExtensionPrefsObserver>
      prefs_observation_{this};
  base::ScopedObservation<extensions::ExtensionManagement,
                          extensions::ExtensionManagement::Observer>
      management_observation_{this};
  base::WeakPtrFactory<NativeExtensionStorageAdapter> weak_factory_{this};
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_NATIVE_EXTENSION_STORAGE_ADAPTER_H_
