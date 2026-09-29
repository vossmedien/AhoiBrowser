// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/workspace_zone_retirement.h"

#include <optional>
#include <set>
#include <utility>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {
namespace {

constexpr char kWorkspaceKey[] = "workspace_id";
constexpr char kRequestedAtKey[] = "requested_at";
constexpr base::TimeDelta kStartupDelay = base::Seconds(30);

struct Entry {
  base::Uuid workspace_id;
  base::Time requested_at;
};

std::optional<Entry> Decode(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString(kWorkspaceKey);
  const std::optional<base::Time> time =
      base::ValueToTime(dict->Find(kRequestedAtKey));
  if (!id || !time) {
    return std::nullopt;
  }
  Entry entry{.workspace_id = base::Uuid::ParseLowercase(*id),
              .requested_at = *time};
  if (!entry.workspace_id.is_valid()) {
    return std::nullopt;
  }
  return entry;
}

std::vector<Entry> Entries(const PrefService* local_state) {
  std::vector<Entry> entries;
  if (!local_state ||
      !local_state->FindPreference(kPendingWorkspaceZoneRetirementsPref)) {
    return entries;
  }
  for (const base::Value& value :
       local_state->GetList(kPendingWorkspaceZoneRetirementsPref)) {
    if (std::optional<Entry> entry = Decode(value)) {
      entries.push_back(std::move(*entry));
    }
  }
  return entries;
}

bool CanWrite(const PrefService* local_state) {
  return local_state &&
         local_state->FindPreference(kPendingWorkspaceZoneRetirementsPref) &&
         !local_state->IsManagedPreference(
             kPendingWorkspaceZoneRetirementsPref);
}

std::set<base::Uuid>& InFlight() {
  static base::NoDestructor<std::set<base::Uuid>> in_flight;
  return *in_flight;
}

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

void RunDueRetirements() {
  PrefService* local_state = LocalState();
  for (const base::Uuid& id :
       GetDueWorkspaceZoneRetirements(local_state, base::Time::Now())) {
    const std::optional<session::IsolatedProfileEntry> live =
        session::FindIsolatedProfileByWorkspaceId(local_state, id);
    if (live && live->state != session::IsolatedProfileState::kDeleting) {
      // Never delete the zone of a Workspace that exists (again).
      CompleteWorkspaceZoneRetirement(local_state, id);
      continue;
    }
    if (!InFlight().insert(id).second) {
      continue;
    }
    RetireWorkspaceZone(id, base::BindOnce(
                                [](base::Uuid id, bool retired) {
                                  InFlight().erase(id);
                                  if (retired) {
                                    CompleteWorkspaceZoneRetirement(
                                        LocalState(), id);
                                  }
                                },
                                id));
  }
}

}  // namespace

bool ScheduleWorkspaceZoneRetirement(PrefService* local_state,
                                     const base::Uuid& workspace_id,
                                     base::Time now) {
  if (!CanWrite(local_state) || !workspace_id.is_valid() || now.is_null()) {
    return false;
  }
  for (const Entry& entry : Entries(local_state)) {
    if (entry.workspace_id == workspace_id) {
      return true;
    }
  }
  base::ListValue list =
      local_state->GetList(kPendingWorkspaceZoneRetirementsPref).Clone();
  list.Append(base::DictValue()
                  .Set(kWorkspaceKey, workspace_id.AsLowercaseString())
                  .Set(kRequestedAtKey, base::TimeToValue(now)));
  local_state->SetList(kPendingWorkspaceZoneRetirementsPref, std::move(list));
  return true;
}

std::vector<base::Uuid> GetDueWorkspaceZoneRetirements(
    const PrefService* local_state,
    base::Time now) {
  std::vector<base::Uuid> due;
  for (const Entry& entry : Entries(local_state)) {
    if (entry.requested_at <= now &&
        now - entry.requested_at >= kTombstoneRetention) {
      due.push_back(entry.workspace_id);
    }
  }
  return due;
}

std::vector<base::Uuid> GetPendingWorkspaceZoneRetirements(
    const PrefService* local_state) {
  std::vector<base::Uuid> pending;
  for (const Entry& entry : Entries(local_state)) {
    pending.push_back(entry.workspace_id);
  }
  return pending;
}

bool CompleteWorkspaceZoneRetirement(PrefService* local_state,
                                     const base::Uuid& workspace_id) {
  if (!CanWrite(local_state) || !workspace_id.is_valid()) {
    return false;
  }
  base::ListValue list =
      local_state->GetList(kPendingWorkspaceZoneRetirementsPref).Clone();
  const size_t removed = list.EraseIf([&workspace_id](const base::Value& v) {
    const std::optional<Entry> entry = Decode(v);
    return entry && entry->workspace_id == workspace_id;
  });
  if (removed) {
    local_state->SetList(kPendingWorkspaceZoneRetirementsPref,
                         std::move(list));
  }
  return removed > 0;
}

bool WorkspaceProfileMayHaveSyncedData(const PrefService& profile_prefs) {
  return profile_prefs.GetBoolean(kSyncEnabledPref) ||
         !profile_prefs.GetString(kDeviceIdPref).empty();
}

WorkspaceZoneRetirementObserver::WorkspaceZoneRetirementObserver(
    PrefService* local_state,
    PrefService* profile_prefs,
    std::string profile_dir,
    base::Uuid workspace_id)
    : local_state_(local_state),
      profile_prefs_(profile_prefs),
      profile_dir_(std::move(profile_dir)),
      workspace_id_(std::move(workspace_id)) {
  if (!local_state_ || !profile_prefs_ ||
      !local_state_->FindPreference(session::kIsolatedProfilesPref)) {
    return;
  }
  registrar_.Init(local_state_);
  registrar_.Add(
      session::kIsolatedProfilesPref,
      base::BindRepeating(&WorkspaceZoneRetirementObserver::OnRegistryChanged,
                          base::Unretained(this)));
}

WorkspaceZoneRetirementObserver::~WorkspaceZoneRetirementObserver() = default;

void WorkspaceZoneRetirementObserver::OnRegistryChanged() {
  const std::optional<session::IsolatedProfileEntry> entry =
      session::FindIsolatedProfile(local_state_, profile_dir_);
  if (!entry || entry->workspace_id != workspace_id_ ||
      entry->state != session::IsolatedProfileState::kDeleting ||
      !WorkspaceProfileMayHaveSyncedData(*profile_prefs_)) {
    return;
  }
  ScheduleWorkspaceZoneRetirement(local_state_, workspace_id_,
                                  base::Time::Now());
}

void ScheduleDueWorkspaceZoneRetirementsOnce() {
  static bool scheduled = false;
  if (scheduled || !base::SequencedTaskRunner::HasCurrentDefault()) {
    return;
  }
  scheduled = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&RunDueRetirements), kStartupDelay);
}

#if !BUILDFLAG(IS_MAC)
void RetireWorkspaceZone(const base::Uuid& workspace_id,
                         base::OnceCallback<void(bool)> done) {
  std::move(done).Run(false);
}
#endif

}  // namespace ahoi::sync
