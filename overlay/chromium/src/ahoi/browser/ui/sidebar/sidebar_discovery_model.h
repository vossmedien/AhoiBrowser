// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_MODEL_H_
#define AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_MODEL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/tab_restore_service_observer.h"
#include "url/gurl.h"

namespace sessions {
class LiveTabContext;
class TabRestoreService;
namespace tab_restore {
struct Entry;
}
}  // namespace sessions

namespace ahoi::sidebar {

enum class SidebarDiscoveryItemKind {
  kOpenTab = 0,
  kSleepingTab,
  kSavedPage,
  kFolder,
  kWorkspace,
  kDeviceTab,
  kRecentlyClosedTab,
  kRecentlyClosedSplit,
  kRecentlyClosedGroup,
  kRecentlyClosedWindow,
};

namespace internal {

// Kept as a narrow testable policy seam: Ahoi-persisted saved pages must never
// be projected as a second recently-closed archive.
bool IsEligibleRecentlyClosedEntry(const sessions::tab_restore::Entry& entry);

}  // namespace internal

// Immutable row data. `stable_id` is a semantic entity identity: running and
// durable projections of the same saved page deliberately alias, while two
// temporary tabs with the same URL remain distinct. Command activation keeps
// Chromium/Ahoi's original stable identity; recently closed activation keeps
// only TabRestoreService's opaque SessionID.
struct SidebarDiscoveryItem {
  SidebarDiscoveryItemKind kind = SidebarDiscoveryItemKind::kOpenTab;
  std::string stable_id;
  std::u16string title;
  std::u16string secondary_text;
  std::optional<GURL> url;
  std::optional<CommandItem> command;
  std::optional<SessionID> restore_id;
  size_t tab_count = 1;
  base::Time timestamp;

  bool operator==(const SidebarDiscoveryItem&) const = default;
};

class SidebarDiscoveryModelObserver : public base::CheckedObserver {
 public:
  virtual void OnSidebarDiscoveryModelChanged() = 0;

 protected:
  ~SidebarDiscoveryModelObserver() override = default;
};

// Profile-local, network-free adapter shared by the native sidebar search and
// recently-closed surface. It observes the existing CommandService index and
// Chromium's TabRestoreService; it owns no browser history or persistence.
class SidebarDiscoveryModel final : public CommandServiceObserver,
                                    public sessions::TabRestoreServiceObserver {
 public:
  SidebarDiscoveryModel(CommandService* command_service,
                        sessions::TabRestoreService* tab_restore_service);
  SidebarDiscoveryModel(const SidebarDiscoveryModel&) = delete;
  SidebarDiscoveryModel& operator=(const SidebarDiscoveryModel&) = delete;
  ~SidebarDiscoveryModel() override;

  void AddObserver(SidebarDiscoveryModelObserver* observer);
  void RemoveObserver(SidebarDiscoveryModelObserver* observer);

  std::vector<SidebarDiscoveryItem> Search(std::u16string_view query,
                                           size_t max_results) const;
  std::vector<SidebarDiscoveryItem> RecentlyClosed(size_t max_results) const;

  // Revalidates that the opaque entry is still owned by the observed service
  // immediately before restore. Passing a null context deliberately preserves
  // Chromium's documented behavior of creating a suitable browser window.
  [[nodiscard]] bool RestoreRecentlyClosed(
      SessionID entry_id,
      sessions::LiveTabContext* live_tab_context);

  // CommandServiceObserver:
  void OnCommandIndexChanged(CommandItemType type) override;

  // sessions::TabRestoreServiceObserver:
  void TabRestoreServiceChanged(sessions::TabRestoreService* service) override;
  void TabRestoreServiceLoaded(sessions::TabRestoreService* service) override;
  void TabRestoreServiceDestroyed(
      sessions::TabRestoreService* service) override;

 private:
  void ScheduleNotifyChanged();
  void NotifyChanged();

  raw_ptr<CommandService> command_service_ = nullptr;
  raw_ptr<sessions::TabRestoreService> tab_restore_service_ = nullptr;
  base::ObserverList<SidebarDiscoveryModelObserver> observers_;
  bool notify_scheduled_ = false;
  base::WeakPtrFactory<SidebarDiscoveryModel> weak_ptr_factory_{this};
};

}  // namespace ahoi::sidebar

#endif  // AHOI_BROWSER_UI_SIDEBAR_SIDEBAR_DISCOVERY_MODEL_H_
