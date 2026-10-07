// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_H_
#define AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_H_

#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "content/public/browser/storage_partition_config.h"

class BrowserWindowInterface;
class TabStripModel;
namespace tabs {
class TabInterface;
}
namespace ahoi {
class SessionBridge;
}

namespace ahoi::extensions {

// A readback of one native group, never a second store or a sync payload.
// The folder exists only under Open Tabs in this browser window. Saved-page
// parent links and saved/temporary flags are independent of group membership.
struct SidebarTabGroup {
  tab_groups::TabGroupId id;
  base::Uuid workspace_id;
  tab_groups::TabGroupVisualData visuals;
  std::vector<base::WeakPtr<tabs::TabInterface>> members;
};

// SessionBridge owns one adapter per tracked normal window, including windows
// without a mounted sidebar. Native SessionService owns restart persistence;
// no group IDs, native handles or website-session identifiers enter CloudKit.
class TabGroupSidebarAdapter final : public TabStripModelObserver {
 public:
  TabGroupSidebarAdapter(SessionBridge& bridge,
                         BrowserWindowInterface& browser,
                         base::RepeatingClosure presentation_changed);
  ~TabGroupSidebarAdapter() override;

  base::WeakPtr<TabGroupSidebarAdapter> GetWeakPtr();
  std::vector<SidebarTabGroup> ReadGroups(const base::Uuid& workspace) const;
  bool SetVisuals(tab_groups::TabGroupId group,
                  const tab_groups::TabGroupVisualData& visuals);
  bool CanAddTab(tab_groups::TabGroupId group, tabs::TabInterface* tab) const;
  bool AddTab(tab_groups::TabGroupId group, tabs::TabInterface* tab);
  bool CanRemoveTab(tabs::TabInterface* tab) const;
  bool RemoveTab(tabs::TabInterface* tab);
  // Removing a folder means Ungroup, never close/delete saved pages.
  bool Ungroup(tab_groups::TabGroupId group);
  // Keep the inline title editor alive across passive Sidebar refreshes.
  void SetTitleEdit(std::optional<tab_groups::TabGroupId> group);
  bool IsTitleEditing(const base::Uuid& workspace) const;

 private:
  struct Scope {
    base::Uuid workspace;
    content::StoragePartitionConfig partition;
    bool operator==(const Scope&) const = default;
  };
  std::optional<Scope> ScopeForTab(tabs::TabInterface* tab) const;
  std::optional<SidebarTabGroup> ReadGroup(tab_groups::TabGroupId group) const;
  // A split is indivisible here: partial native group mutations unsplit it.
  std::vector<tabs::TabInterface*> UnitForTab(tabs::TabInterface* tab) const;
  std::vector<int> IndicesFor(
      const std::vector<tabs::TabInterface*>& tabs) const;
  bool CanRead() const;
  void ScheduleReconcile(bool notify_presentation = false);
  void Reconcile();

  void OnTabStripModelChanged(TabStripModel*,
                             const TabStripModelChange&,
                             const TabStripSelectionChange&) override;
  void TabGroupedStateChanged(TabStripModel*,
                             std::optional<tab_groups::TabGroupId>,
                             std::optional<tab_groups::TabGroupId>,
                             tabs::TabInterface*, int) override;
  void OnTabGroupChanged(const TabGroupChange&) override;
  void OnSplitTabChanged(const SplitTabChange&) override;
  void OnTabChangedAt(tabs::TabInterface*, TabChangeType) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;

  const raw_ptr<SessionBridge> bridge_;
  const base::WeakPtr<BrowserWindowInterface> browser_;
  const base::RepeatingClosure presentation_changed_;
  raw_ptr<TabStripModel> model_ = nullptr;
  base::CallbackListSubscription runtime_subscription_;
  base::CallbackListSubscription tree_subscription_;
  base::CallbackListSubscription restored_subscription_;
  bool scheduled_ = false;
  bool reconciling_ = false;
  bool notify_pending_ = false;
  std::optional<tab_groups::TabGroupId> editing_group_;
  base::WeakPtrFactory<TabGroupSidebarAdapter> weak_ptr_factory_{this};
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_TAB_GROUP_SIDEBAR_ADAPTER_H_
