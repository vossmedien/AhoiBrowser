// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_WORKSPACE_ZONE_RETIREMENT_H_
#define AHOI_BROWSER_SYNC_WORKSPACE_ZONE_RETIREMENT_H_

#include <memory>
#include <string>
#include <vector>

#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/prefs/pref_change_registrar.h"

class PrefRegistrySimple;
class PrefService;

namespace ahoi::sync {

// ADR 0011 step 4, WS-ISO-20. Deleting a fully separated Workspace tombstones
// it in its own zone; once the format-3 tombstone retention has passed, the
// whole zone `<main zone>-ws-<uuid>` is deleted and its key retired, so no
// data of a deleted Workspace stays in iCloud. The pending retirements live in
// Local State, so they survive the Profile's deletion and a crash.
// kPendingWorkspaceZoneRetirementsPref is registered by
// RegisterLocalStatePrefs() (profile_sync_prefs.h) from the browser's Local
// State registration; without it every function below is inert.

// Records a pending retirement requested at `now`. Idempotent: an existing
// entry keeps its earlier request time. False for an invalid id or an
// unregistered/managed preference.
bool ScheduleWorkspaceZoneRetirement(PrefService* local_state,
                                     const base::Uuid& workspace_id,
                                     base::Time now);
// Workspaces whose retention (kTombstoneRetention) has passed at `now`, in
// stored order. A request time in the future (clock moved back) is not due.
std::vector<base::Uuid> GetDueWorkspaceZoneRetirements(
    const PrefService* local_state,
    base::Time now);
std::vector<base::Uuid> GetPendingWorkspaceZoneRetirements(
    const PrefService* local_state);
// Removes the entry once its zone is gone and its key retired.
bool CompleteWorkspaceZoneRetirement(PrefService* local_state,
                                     const base::Uuid& workspace_id);

// True when the separated Profile ever enabled sync, so its zone may exist.
// The device id is persisted only once sync was enabled; a Profile that never
// synced leaves nothing in iCloud and schedules no CloudKit call.
bool WorkspaceProfileMayHaveSyncedData(const PrefService& profile_prefs);

// Owned by a separated Profile's sync service. When the Local State registry
// marks this Profile `deleting` (session::DeleteIsolatedWorkspaceProfile), it
// schedules the retirement synchronously inside the same pref write, so the
// deletion's CommitPendingWrite persists both together.
class WorkspaceZoneRetirementObserver {
 public:
  WorkspaceZoneRetirementObserver(PrefService* local_state,
                                  PrefService* profile_prefs,
                                  std::string profile_dir,
                                  base::Uuid workspace_id);
  WorkspaceZoneRetirementObserver(const WorkspaceZoneRetirementObserver&) =
      delete;
  WorkspaceZoneRetirementObserver& operator=(
      const WorkspaceZoneRetirementObserver&) = delete;
  ~WorkspaceZoneRetirementObserver();

 private:
  void OnRegistryChanged();

  const raw_ptr<PrefService> local_state_;
  const raw_ptr<PrefService> profile_prefs_;
  const std::string profile_dir_;
  const base::Uuid workspace_id_;
  PrefChangeRegistrar registrar_;
};

// Once per browser process, after a short delay: retires every due zone
// through the platform transport. An entry whose Workspace is live again in
// the registry is dropped without touching CloudKit. Failures keep the entry
// for the next process. No-op without a CloudKit transport.
void ScheduleDueWorkspaceZoneRetirementsOnce();

// Platform step: deletes the Workspace's zone and retires its key. `done`
// receives true only when both are gone (a missing zone/key counts as gone).
// Never touches the main namespace. Implemented on macOS; elsewhere false.
void RetireWorkspaceZone(const base::Uuid& workspace_id,
                         base::OnceCallback<void(bool)> done);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_WORKSPACE_ZONE_RETIREMENT_H_
