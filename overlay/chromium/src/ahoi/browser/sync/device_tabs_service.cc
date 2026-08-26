// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#include "ahoi/browser/sync/device_tabs_service.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "ahoi/browser/sync/sync_policy.h"
#include "base/check.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

bool IsSafeRemoteTab(const RemoteTabRecord& tab) {
  const GURL url(tab.url);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty() &&
         !url.has_username() && !url.has_password();
}

}  // namespace

DeviceTabsService::DeviceTabsService(SyncStore* store,
                                     base::Uuid local_device_id)
    : store_(store), local_device_id_(std::move(local_device_id)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(store_);
  CHECK(local_device_id_.is_valid());
  store_->AddObserver(this);
  std::ignore = Refresh();
}

DeviceTabsService::DeviceTabsService(std::unique_ptr<SyncStore> owned_store,
                                     base::Uuid local_device_id)
    : owned_store_(std::move(owned_store)),
      store_(owned_store_.get()),
      local_device_id_(std::move(local_device_id)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(store_);
  CHECK(local_device_id_.is_valid());
  store_->AddObserver(this);
  std::ignore = Refresh();
}

DeviceTabsService::~DeviceTabsService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (store_) {
    store_->RemoveObserver(this);
  }
}

std::unique_ptr<DeviceTabsService> DeviceTabsService::CreateForTesting(
    std::unique_ptr<SyncStore> store,
    base::Uuid local_device_id) {
  return std::make_unique<DeviceTabsService>(std::move(store),
                                             std::move(local_device_id));
}

void DeviceTabsService::Subscribe(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
  observer->OnDeviceTabsSnapshot(snapshot_);
}

void DeviceTabsService::Unsubscribe(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

SyncStore::Result DeviceTabsService::Refresh() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!store_) {
    return SyncStore::Result::kNotInitialized;
  }
  std::vector<RemoteTabRecord> all_tabs;
  const SyncStore::Result result = store_->GetRemoteTabs(&all_tabs);
  if (result != SyncStore::Result::kOk) {
    return result;
  }

  DeviceTabsSnapshot next;
  std::map<base::Uuid, DeviceSessionRecord> active_sessions;
  std::vector<SyncRecord> records;
  if (store_->GetRecords(EntityType::kDeviceSession, &records) !=
      SyncStore::Result::kOk) {
    return SyncStore::Result::kDatabaseError;
  }
  const base::Time now = base::Time::Now();
  const base::Time oldest_active_session = now - kRemoteSessionVisibleAge;
  for (SyncRecord& record : records) {
    if (DeviceSessionRecord* session =
            std::get_if<DeviceSessionRecord>(&record);
        session && session->active && !session->tombstone &&
        session->last_seen >= oldest_active_session) {
      active_sessions.insert_or_assign(session->id, std::move(*session));
    }
  }
  records.clear();
  if (store_->GetRecords(EntityType::kDevice, &records) !=
      SyncStore::Result::kOk) {
    return SyncStore::Result::kDatabaseError;
  }
  std::set<base::Uuid> available_devices;
  for (SyncRecord& record : records) {
    if (DeviceRecord* device = std::get_if<DeviceRecord>(&record);
        device && !device->tombstone && !device->retired) {
      available_devices.insert(device->id);
      next.devices.push_back(std::move(*device));
    }
  }
  for (const auto& [id, session] : active_sessions) {
    next.sessions.push_back(session);
  }
  records.clear();
  if (store_->GetRecords(EntityType::kWorkspace, &records) !=
      SyncStore::Result::kOk) {
    return SyncStore::Result::kDatabaseError;
  }
  for (SyncRecord& record : records) {
    if (WorkspaceRecord* workspace = std::get_if<WorkspaceRecord>(&record);
        workspace && !workspace->tombstone) {
      next.workspaces.push_back(std::move(*workspace));
    }
  }
  for (RemoteTabRecord& tab : all_tabs) {
    if (tab.tombstone || tab.is_incognito || !tab.device_id.is_valid() ||
        !tab.session_id.is_valid() || !IsSafeRemoteTab(tab)) {
      continue;
    }
    auto session = active_sessions.find(tab.session_id);
    if (session == active_sessions.end() ||
        session->second.device_id != tab.device_id) {
      continue;
    }
    if (tab.device_id == local_device_id_) {
      next.local_tabs.push_back(std::move(tab));
    } else if (available_devices.contains(tab.device_id)) {
      next.remote_tabs.push_back(std::move(tab));
    }
  }
  const auto order = [](const RemoteTabRecord& left,
                        const RemoteTabRecord& right) {
    if (left.last_active != right.last_active) {
      return left.last_active > right.last_active;
    }
    return left.id < right.id;
  };
  std::ranges::sort(next.local_tabs, order);
  std::ranges::sort(next.remote_tabs, order);
  std::ranges::sort(next.devices, {}, &DeviceRecord::id);
  std::ranges::sort(next.sessions, {}, &DeviceSessionRecord::id);
  std::ranges::sort(next.workspaces, {}, &WorkspaceRecord::id);
  if (next != snapshot_) {
    snapshot_ = std::move(next);
    Publish();
  }
  return SyncStore::Result::kOk;
}

bool DeviceTabsService::IsRemoteSessionActionable(
    const DeviceSessionRecord& session,
    base::Time now) {
  return session.active && !session.tombstone && !now.is_null() &&
         session.last_seen >= now - kRemoteSessionActionableAge;
}

const DeviceTabsSnapshot& DeviceTabsService::GetSnapshot() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return snapshot_;
}

SyncStore::Result DeviceTabsService::UpsertLocalTab(
    const RemoteTabRecord& tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab.device_id != local_device_id_ || tab.is_incognito || tab.tombstone) {
    return SyncStore::Result::kInvalidArgument;
  }
  return store_->PutLocalRecord(tab);
}

SyncStore::Result DeviceTabsService::RemoveLocalTab(
    const RemoteTabRecord& tombstone) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tombstone.device_id != local_device_id_ || tombstone.is_incognito ||
      !tombstone.tombstone) {
    return SyncStore::Result::kInvalidArgument;
  }
  return store_->PutLocalRecord(tombstone);
}

void DeviceTabsService::OnSyncStoreChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::ignore = Refresh();
}

void DeviceTabsService::Publish() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (Observer& observer : observers_) {
    observer.OnDeviceTabsSnapshot(snapshot_);
  }
}

}  // namespace ahoi::sync
