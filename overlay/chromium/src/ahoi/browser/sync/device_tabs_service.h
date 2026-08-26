// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_DEVICE_TABS_SERVICE_H_
#define AHOI_BROWSER_SYNC_DEVICE_TABS_SERVICE_H_

#include <memory>

#include "ahoi/browser/sync/sync_model.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"

namespace ahoi::sync {

// Profile-scoped read/model service for the sidebar. A future
// BrowserContextKeyedServiceFactory can own one instance per profile while
// tests use CreateForTesting() with an in-memory SyncStore.
class DeviceTabsService final : public SyncStoreObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;
    virtual void OnDeviceTabsSnapshot(const DeviceTabsSnapshot& snapshot) = 0;
  };

  DeviceTabsService(SyncStore* store, base::Uuid local_device_id);
  DeviceTabsService(std::unique_ptr<SyncStore> owned_store,
                    base::Uuid local_device_id);
  DeviceTabsService(const DeviceTabsService&) = delete;
  DeviceTabsService& operator=(const DeviceTabsService&) = delete;
  ~DeviceTabsService() override;

  static std::unique_ptr<DeviceTabsService> CreateForTesting(
      std::unique_ptr<SyncStore> store,
      base::Uuid local_device_id);

  void Subscribe(Observer* observer);
  void Unsubscribe(Observer* observer);

  // Re-reads the persistent tab records and publishes one immutable snapshot.
  // The service always filters tombstones, incognito rows, and malformed local
  // device ids before exposing data to the sidebar.
  [[nodiscard]] SyncStore::Result Refresh();
  const DeviceTabsSnapshot& GetSnapshot() const;
  static bool IsRemoteSessionActionable(const DeviceSessionRecord& session,
                                        base::Time now);

  // Convenience write seams for the native tab-strip observer. The caller is
  // responsible for assigning a fresh HLC version to each record.
  [[nodiscard]] SyncStore::Result UpsertLocalTab(const RemoteTabRecord& tab);
  [[nodiscard]] SyncStore::Result RemoveLocalTab(
      const RemoteTabRecord& tombstone);

  SyncStore* store_for_testing() const { return store_; }

  void OnSyncStoreChanged() override;

 private:
  void Publish();

  std::unique_ptr<SyncStore> owned_store_;
  raw_ptr<SyncStore> store_ = nullptr;
  const base::Uuid local_device_id_;
  DeviceTabsSnapshot snapshot_;
  base::ObserverList<Observer> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_DEVICE_TABS_SERVICE_H_
