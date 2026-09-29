// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/functional/bind.h"
namespace ahoi::sync {
void ProfileSyncService::ReadWorkspaceStructure(
    base::OnceCallback<void(std::optional<WorkspaceStructureProjection>)>
        callback) {
  if (!callback)
    return;
  if (!backend_ready_ || !sync_enabled_ || shutting_down_) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ReadWorkspaceStructure)
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> owner, decltype(callback) reply,
             std::optional<WorkspaceStructureProjection> p) {
            if (!owner || !owner->sync_enabled_ || owner->shutting_down_ ||
                !p || !p->authorization || !p->authorization.Run())
              p.reset();
            std::move(reply).Run(std::move(p));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}
void ProfileSyncService::PublishWorkspaceStructureIntent(
    WorkspaceStructureIntent intent,
    base::OnceCallback<void(bool)> callback) {
  if (!callback)
    return;
  if (!backend_ready_ || !sync_enabled_ || shutting_down_ ||
      !intent.authorization || !intent.authorization.Run()) {
    std::move(callback).Run(false);
    return;
  }
  auto original = intent.authorization;
  backend_.AsyncCall(&ProfileSyncBackend::PublishWorkspaceStructureIntent)
      .WithArgs(std::move(intent))
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> owner,
             SyncAuthorization original, decltype(callback) reply,
             std::optional<SyncStateSnapshot> state) {
            if (!owner || !owner->sync_enabled_ || owner->shutting_down_ ||
                !original.Run() || !state) {
              std::move(reply).Run(false);
              return;
            }
            owner->OnBackendState(std::move(state));
            std::move(reply).Run(owner && original.Run());
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(original),
          std::move(callback)));
}
}  // namespace ahoi::sync
