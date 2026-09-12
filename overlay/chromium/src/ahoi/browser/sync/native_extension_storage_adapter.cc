// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_extension_storage_adapter.h"

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/native_extension_storage_io.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/one_shot_event.h"
#include "chrome/browser/profiles/profile.h"
#include "components/sync/base/command_line_switches.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/api/storage/settings_namespace.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_utils.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/api_permission_id.mojom.h"
#include "extensions/common/mojom/manifest.mojom.h"
#include "extensions/common/permissions/permissions_data.h"

namespace ahoi::sync {
namespace {
bool Authorized(const SyncAuthorization& authorization) {
  return authorization && authorization.Run();
}
std::vector<std::string> Keys(std::string_view id) {
  std::vector<std::string> keys;
  for (const auto& descriptor : GetExtensionStorageCatalog()) {
    if (descriptor.extension_id == id) {
      keys.emplace_back(descriptor.key);
    }
  }
  return keys;
}
ExtensionStorageResult Unavailable(const ExtensionStorageRequest& request,
                                   ExtensionStorageDisposition disposition) {
  return {.operation_id = request.operation_id,
          .revision = request.revision,
          .disposition = disposition};
}
}  // namespace

struct NativeExtensionStorageAdapter::ReadBatch {
  size_t remaining = 0;
  NativeExtensionStorageSnapshot snapshot{.complete = true};
  base::OnceCallback<void(NativeExtensionStorageSnapshot)> completion;
};

NativeExtensionStorageAdapter::NativeExtensionStorageAdapter(
    Profile* profile,
    LocalChange changed,
    base::RepeatingClosure ready)
    : profile_(profile),
      changed_(std::move(changed)),
      ready_(std::move(ready)) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile_ || !profile_->IsRegularProfile() ||
      profile_->IsOffTheRecord()) {
    return;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  auto* prefs = extensions::ExtensionPrefs::Get(profile_);
  auto* management =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile_);
  if (!registry || !prefs || !management) {
    return;
  }
  registry_observation_.Observe(registry);
  prefs_observation_.Observe(prefs);
  management_observation_.Observe(management);
  frontend_ = extensions::StorageFrontend::Get(profile_);
  if (!frontend_) {
    return;
  }
  changes_subscription_ = frontend_->ObserveSyncSettingsChanges(
      base::BindRepeating(&NativeExtensionStorageAdapter::OnChanged,
                          weak_factory_.GetWeakPtr()));
  requests_subscription_ =
      frontend_->ObserveSyncSettingsWriteRequests(base::BindRepeating(
          &NativeExtensionStorageAdapter::OnNativeWriteRequested,
          weak_factory_.GetWeakPtr()));
  if (auto* system = extensions::ExtensionSystem::Get(profile_)) {
    system->ready().Post(FROM_HERE,
                         base::BindOnce(&NativeExtensionStorageAdapter::Ready,
                                        weak_factory_.GetWeakPtr()));
  }
}

NativeExtensionStorageAdapter::~NativeExtensionStorageAdapter() {
  Stop();
}
base::WeakPtr<NativeExtensionStorageAdapter>
NativeExtensionStorageAdapter::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void NativeExtensionStorageAdapter::Stop() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  stopped_->store(true, std::memory_order_release);
  weak_factory_.InvalidateWeakPtrs();
  InvalidateAll();
  requests_subscription_ = {};
  changes_subscription_ = {};
  registry_observation_.Reset();
  prefs_observation_.Reset();
  management_observation_.Reset();
  frontend_ = nullptr;
  profile_ = nullptr;
}

scoped_refptr<const extensions::Extension>
NativeExtensionStorageAdapter::Eligible(
    std::string_view id,
    ExtensionStorageDisposition* failure) const {
  auto fail = [failure](ExtensionStorageDisposition reason) {
    if (failure) {
      *failure = reason;
    }
    return scoped_refptr<const extensions::Extension>();
  };
  if (!profile_ || !frontend_ || stopped_->load(std::memory_order_acquire)) {
    return fail(ExtensionStorageDisposition::kDeferred);
  }
  if (!base::CommandLine::InitializedForCurrentProcess() ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          syncer::kDisableSync)) {
    return fail(ExtensionStorageDisposition::kBlockedByPolicy);
  }
  if (!frontend_->IsStorageEnabled(extensions::settings_namespace::SYNC) ||
      Keys(id).empty()) {
    return fail(ExtensionStorageDisposition::kUnsupported);
  }
  auto* system = extensions::ExtensionSystem::Get(profile_);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (!system || !system->is_ready() || !system->management_policy() ||
      !registry) {
    return fail(ExtensionStorageDisposition::kDeferred);
  }
  const auto* extension = registry->GetInstalledExtension(std::string(id));
  if (!extension) {
    return fail(ExtensionStorageDisposition::kDeferred);
  }
  if (!extension->is_extension() || extension->manifest_version() != 3 ||
      extension->location() != extensions::mojom::ManifestLocation::kInternal ||
      !extension->from_webstore() || extension->may_be_untrusted() ||
      !extension->permissions_data()->HasAPIPermission(
          extensions::mojom::APIPermissionID::kStorage)) {
    return fail(ExtensionStorageDisposition::kUnsupported);
  }
  for (const auto& descriptor : GetExtensionStorageCatalog()) {
    if (descriptor.extension_id == id &&
        extension->VersionString() != descriptor.reviewed_version) {
      return fail(ExtensionStorageDisposition::kUnsupported);
    }
  }
  if (!system->management_policy()->UserMayLoad(extension) ||
      !system->management_policy()->UserMayModifySettings(extension, nullptr)) {
    return fail(ExtensionStorageDisposition::kBlockedByPolicy);
  }
  return base::WrapRefCounted(extension);
}

SyncAuthorization NativeExtensionStorageAdapter::CaptureScope(
    const std::string& id,
    SyncAuthorization original) {
  auto& epoch = epochs_[id];
  if (!epoch) {
    epoch = std::make_shared<std::atomic<bool>>(false);
  }
  return base::BindRepeating(
      [](SyncAuthorization original, std::shared_ptr<std::atomic<bool>> stopped,
         std::shared_ptr<std::atomic<bool>> epoch) {
        return !stopped->load(std::memory_order_acquire) &&
               !epoch->load(std::memory_order_acquire) && Authorized(original);
      },
      std::move(original), stopped_, epoch);
}

void NativeExtensionStorageAdapter::Invalidate(const std::string& id) {
  auto found = epochs_.find(id);
  if (found != epochs_.end()) {
    found->second->store(true, std::memory_order_release);
    epochs_.erase(found);
  }
}
void NativeExtensionStorageAdapter::InvalidateAll() {
  for (const auto& [id, epoch] : epochs_) {
    epoch->store(true, std::memory_order_release);
  }
  epochs_.clear();
}
void NativeExtensionStorageAdapter::Ready() {
  const auto ready = ready_;
  if (ready && !stopped_->load(std::memory_order_acquire)) {
    ready.Run();
  }
}

void NativeExtensionStorageAdapter::OnNativeWriteRequested(
    const extensions::ExtensionId& id) {
  // BEFORE the local Set/Remove/Clear enters its backend queue, including an
  // A->X->A sequence whose final value alone cannot reveal the intervening
  // edit.
  InvalidateAndRefresh(id);
}

void NativeExtensionStorageAdapter::InvalidateAndRefresh(
    const std::string& id) {
  if (Keys(id).empty()) {
    return;
  }
  Invalidate(id);
  Ready();  // Asynchronous Service refresh; do not author until actual success.
}

void NativeExtensionStorageAdapter::OnChanged(
    const extensions::ExtensionId& id,
    const base::DictValue& changes,
    extensions::StorageFrontend::SyncSettingsChangeOrigin origin) {
  if (origin !=
          extensions::StorageFrontend::SyncSettingsChangeOrigin::kNative ||
      !Eligible(id)) {
    return;
  }
  const auto changed = changed_;
  const auto lifetime = weak_factory_.GetWeakPtr();
  for (const auto& key : Keys(id)) {
    const auto* delta = changes.FindDict(key);
    if (!delta) {
      continue;
    }
    const auto* next = delta->Find("newValue");
    const auto* old = delta->Find("oldValue");
    if ((next && !next->is_bool()) || (!next && (!old || !old->is_bool()))) {
      continue;
    }
    if (changed) {
      changed.Run(
          {.extension_id = id,
           .key = key,
           .value = next ? std::make_optional(next->GetBool()) : std::nullopt});
    }
    if (!lifetime || !Eligible(id)) {
      return;
    }
  }
}

void NativeExtensionStorageAdapter::Read(
    SyncAuthorization authorization,
    base::OnceCallback<void(NativeExtensionStorageSnapshot)> completion) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile_ || !frontend_ || !Authorized(authorization) ||
      !extensions::ExtensionSystem::Get(profile_) ||
      !extensions::ExtensionSystem::Get(profile_)->is_ready() ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          syncer::kDisableSync)) {
    std::move(completion).Run({});
    return;
  }
  std::set<std::string> ids;
  for (const auto& descriptor : GetExtensionStorageCatalog()) {
    ids.emplace(descriptor.extension_id);
  }
  auto batch = std::make_shared<ReadBatch>();
  batch->completion = std::move(completion);
  std::vector<scoped_refptr<const extensions::Extension>> eligible;
  for (const auto& id : ids) {
    if (auto extension = Eligible(id)) {
      eligible.push_back(std::move(extension));
    }
  }
  batch->remaining = eligible.size();
  if (eligible.empty()) {
    std::move(batch->completion).Run(std::move(batch->snapshot));
    return;
  }
  for (const auto& extension : eligible) {
    auto scope = CaptureScope(extension->id(), authorization);
    frontend_->RunWithStorage(
        extension, extensions::settings_namespace::SYNC,
        base::BindOnce(&ReadNativeExtensionStorage, extension->id(),
                       Keys(extension->id()), scope,
                       base::BindOnce(&NativeExtensionStorageAdapter::OnRead,
                                      weak_factory_.GetWeakPtr(), batch,
                                      extension, scope)));
  }
}

void NativeExtensionStorageAdapter::OnRead(
    std::shared_ptr<ReadBatch> batch,
    scoped_refptr<const extensions::Extension> expected,
    SyncAuthorization authorization,
    NativeExtensionStorageSnapshot snapshot) {
  if (!Authorized(authorization) ||
      Eligible(expected->id()).get() != expected.get() || !snapshot.complete) {
    batch->snapshot.complete = false;
  } else {
    for (auto& value : snapshot.values) {
      batch->snapshot.values.push_back(std::move(value));
    }
  }
  if (--batch->remaining == 0) {
    if (!batch->snapshot.complete) {
      batch->snapshot.values.clear();
    }
    std::move(batch->completion).Run(std::move(batch->snapshot));
  }
}

void NativeExtensionStorageAdapter::Apply(
    ExtensionStorageRequest request,
    base::OnceCallback<void(ExtensionStorageResult)> completion) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto failure = ExtensionStorageDisposition::kUnsupported;
  auto extension = Eligible(request.desired.extension_id, &failure);
  if (!IsValidExtensionStorageValue(request.desired) ||
      !request.operation_id.is_valid() || request.revision.empty() ||
      !extension || !Authorized(request.authorization)) {
    std::move(completion)
        .Run(Unavailable(request, !Authorized(request.authorization)
                                      ? ExtensionStorageDisposition::kCancelled
                                      : failure));
    return;
  }
  request.authorization = CaptureScope(extension->id(), request.authorization);
  // This read/reply fence also lets earlier Native write completions publish
  // their actual local intents before we enqueue the remote mutation. Later
  // Native requests revoke this captured epoch immediately on UI.
  frontend_->RunWithStorage(
      extension, extensions::settings_namespace::SYNC,
      base::BindOnce(
          &ReadNativeExtensionStorage, extension->id(),
          std::vector<std::string>{request.desired.key}, request.authorization,
          base::BindOnce(&NativeExtensionStorageAdapter::AfterApplyRead,
                         weak_factory_.GetWeakPtr(), request, extension,
                         std::move(completion))));
}

void NativeExtensionStorageAdapter::AfterApplyRead(
    ExtensionStorageRequest request,
    scoped_refptr<const extensions::Extension> expected,
    base::OnceCallback<void(ExtensionStorageResult)> completion,
    NativeExtensionStorageSnapshot snapshot) {
  auto failure = ExtensionStorageDisposition::kCancelled;
  auto current = Eligible(expected->id(), &failure);
  if (!snapshot.complete || !Authorized(request.authorization) ||
      current.get() != expected.get()) {
    const auto disposition = !Authorized(request.authorization)
                                 ? ExtensionStorageDisposition::kCancelled
                             : !snapshot.complete && current
                                 ? ExtensionStorageDisposition::kFailed
                                 : failure;
    std::move(completion).Run(Unavailable(request, disposition));
    return;
  }
  const auto notify = frontend_->GetRemoteSyncApplyObserver();
  frontend_->RunWithStorage(
      expected, extensions::settings_namespace::SYNC,
      base::BindOnce(&WriteNativeExtensionStorage, request,
                     base::BindOnce(&NativeExtensionStorageAdapter::OnWritten,
                                    weak_factory_.GetWeakPtr(), request,
                                    expected, notify, std::move(completion))));
}

void NativeExtensionStorageAdapter::OnWritten(
    ExtensionStorageRequest request,
    scoped_refptr<const extensions::Extension> expected,
    extensions::SettingsChangedCallback notify,
    base::OnceCallback<void(ExtensionStorageResult)> completion,
    NativeExtensionStorageCommit committed) {
  const auto lifetime = weak_factory_.GetWeakPtr();
  if (profile_ && Eligible(expected->id()).get() == expected.get() &&
      committed.changes.is_dict() && !committed.changes.GetDict().empty()) {
    // A real committed write still has a native JS event even if consent was
    // revoked after commit. kRemoteApply prevents its reauthoring; current JS
    // access restrictions are preserved. Never notify a replacement extension.
    const auto access = extensions::storage_utils::GetAccessLevelForArea(
        expected->id(), *profile_, extensions::StorageAreaNamespace::kSync);
    notify.Run(expected->id(), extensions::StorageAreaNamespace::kSync, access,
               std::move(committed.changes));
  }
  if (!lifetime) {
    return;
  }
  if (!Authorized(request.authorization) ||
      Eligible(expected->id()).get() != expected.get()) {
    committed.result.disposition = ExtensionStorageDisposition::kCancelled;
    committed.result.readback.reset();
  }
  std::move(completion).Run(std::move(committed.result));
}

void NativeExtensionStorageAdapter::OnExtensionManagementSettingsChanged() {
  InvalidateAll();
  Ready();
}
void NativeExtensionStorageAdapter::OnExtensionLoaded(
    content::BrowserContext*,
    const extensions::Extension* extension) {
  InvalidateAndRefresh(extension->id());
}
void NativeExtensionStorageAdapter::OnExtensionUnloaded(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason) {
  InvalidateAndRefresh(extension->id());
}
void NativeExtensionStorageAdapter::OnExtensionInstalled(
    content::BrowserContext*,
    const extensions::Extension* extension,
    bool) {
  InvalidateAndRefresh(extension->id());
}
void NativeExtensionStorageAdapter::OnExtensionUninstalled(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UninstallReason) {
  InvalidateAndRefresh(extension->id());
}
void NativeExtensionStorageAdapter::OnShutdown(extensions::ExtensionRegistry*) {
  Stop();
}
void NativeExtensionStorageAdapter::OnExtensionPrefsUpdated(
    const extensions::ExtensionId& id) {
  InvalidateAndRefresh(id);
}
void NativeExtensionStorageAdapter::OnExtensionRuntimePermissionsChanged(
    const extensions::ExtensionId& id) {
  InvalidateAndRefresh(id);
}
void NativeExtensionStorageAdapter::OnExtensionPrefsWillBeDestroyed(
    extensions::ExtensionPrefs*) {
  Stop();
}

}  // namespace ahoi::sync
