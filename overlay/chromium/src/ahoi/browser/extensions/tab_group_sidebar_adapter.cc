// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/tab_group_sidebar_adapter.h"

#include <algorithm>
#include <utility>

#include "ahoi/browser/session/session_bridge.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"

namespace ahoi::extensions {

TabGroupSidebarAdapter::TabGroupSidebarAdapter(
    SessionBridge& bridge, BrowserWindowInterface& browser,
    base::RepeatingClosure presentation_changed)
    : bridge_(&bridge), browser_(browser.GetWeakPtr()),
      presentation_changed_(std::move(presentation_changed)),
      model_(browser.GetTabStripModel()) {
  model_->AddObserver(this);
  runtime_subscription_ = bridge.AddRuntimePresentationChangedCallback(
      base::BindRepeating(&TabGroupSidebarAdapter::ScheduleReconcile,
                          weak_ptr_factory_.GetWeakPtr(), false));
  tree_subscription_ = bridge.AddTabTreeSnapshotChangedCallback(
      base::BindRepeating(
          [](base::WeakPtr<TabGroupSidebarAdapter> adapter,
             const tab_tree::TabTreeSnapshot&) {
            if (adapter) {
              adapter->ScheduleReconcile();
            }
          }, weak_ptr_factory_.GetWeakPtr()));
  restored_subscription_ = SessionRestore::RegisterOnSessionRestoredCallback(
      base::BindRepeating(
          [](base::WeakPtr<TabGroupSidebarAdapter> adapter, Profile* profile,
             int) {
            if (adapter && adapter->browser_ &&
                adapter->browser_->GetProfile() == profile) {
              // The callback still runs inside SessionRestore's lifetime.
              adapter->ScheduleReconcile(true);
            }
          }, weak_ptr_factory_.GetWeakPtr()));
  ScheduleReconcile(true);
}

TabGroupSidebarAdapter::~TabGroupSidebarAdapter() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

base::WeakPtr<TabGroupSidebarAdapter> TabGroupSidebarAdapter::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool TabGroupSidebarAdapter::CanRead() const {
  return browser_ && !browser_->IsDeleteScheduled() && model_ &&
         model_->SupportsTabGroups() && bridge_->is_ready() &&
         !browser_->GetProfile()->IsOffTheRecord() &&
         !SessionRestore::IsRestoring(browser_->GetProfile());
}

std::optional<TabGroupSidebarAdapter::Scope>
TabGroupSidebarAdapter::ScopeForTab(tabs::TabInterface* tab) const {
  if (!CanRead() || !tab || tab->GetBrowserWindowInterface() != browser_.get() ||
      tab->GetProfile() != browser_->GetProfile() ||
      bridge_->FindTabStripModelForTab(tab) != model_ ||
      !bridge_->FindSharedTreeNodeIdForTab(tab)) {
    return std::nullopt;
  }
  const auto workspace = bridge_->GetWorkspaceForTab(tab);
  auto* contents = tab->GetContents();
  if (!workspace || !workspace->is_valid() || !contents ||
      !contents->GetPrimaryMainFrame()) {
    return std::nullopt;
  }
  auto* site = contents->GetPrimaryMainFrame()->GetSiteInstance();
  if (!site || site->GetBrowserContext() != browser_->GetProfile()) {
    return std::nullopt;
  }
  auto* partition = browser_->GetProfile()->GetStoragePartition(site);
  if (!partition) {
    return std::nullopt;
  }
  // Compare the actual native partition, including extension partitions.
  // Never reinterpret a foreign domain as the default website session.
  return Scope{*workspace, partition->GetConfig()};
}

std::optional<SidebarTabGroup> TabGroupSidebarAdapter::ReadGroup(
    tab_groups::TabGroupId group) const {
  if (!CanRead() || !model_->group_model()->ContainsTabGroup(group)) {
    return std::nullopt;
  }
  const auto* native = model_->group_model()->GetTabGroup(group);
  if (!native->visual_data() || native->IsEmpty()) {
    return std::nullopt;
  }
  SidebarTabGroup result{group, {}, *native->visual_data(), {}};
  std::optional<Scope> owner;
  for (auto* tab : *model_) {
    if (tab->GetGroup() != group) {
      continue;
    }
    const auto scope = ScopeForTab(tab);
    if (!scope || (owner && *owner != *scope)) {
      return std::nullopt;
    }
    owner = scope;
    result.members.push_back(tab->GetWeakPtr());
  }
  if (!owner || result.members.empty()) {
    return std::nullopt;
  }
  result.workspace_id = owner->workspace;
  return result;
}

std::vector<SidebarTabGroup> TabGroupSidebarAdapter::ReadGroups(
    const base::Uuid& workspace) const {
  std::vector<SidebarTabGroup> groups;
  if (!CanRead()) {
    return groups;
  }
  // Native order, no persistent mirror and no title/URL deduplication.
  for (const auto& id : model_->group_model()->ListTabGroups()) {
    auto group = ReadGroup(id);
    if (group && group->workspace_id == workspace) {
      groups.push_back(std::move(*group));
    }
  }
  return groups;
}

std::vector<tabs::TabInterface*> TabGroupSidebarAdapter::UnitForTab(
    tabs::TabInterface* tab) const {
  const auto scope = ScopeForTab(tab);
  if (!scope) {
    return {};
  }
  std::vector<tabs::TabInterface*> unit{tab};
  if (const auto split_id = tab->GetSplit()) {
    const auto* split = model_->GetSplitData(*split_id);
    if (!split) {
      return {};
    }
    unit = split->ListTabs();
  }
  for (auto* member : unit) {
    if (ScopeForTab(member) != scope) {
      return {};
    }
  }
  return unit;
}

std::vector<int> TabGroupSidebarAdapter::IndicesFor(
    const std::vector<tabs::TabInterface*>& tabs) const {
  std::vector<int> indices;
  for (auto* tab : tabs) {
    const int index = model_->GetIndexOfTab(tab);
    if (index < 0) {
      return {};
    }
    indices.push_back(index);
  }
  std::ranges::sort(indices);
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  return indices;
}

bool TabGroupSidebarAdapter::SetVisuals(
    tab_groups::TabGroupId group,
    const tab_groups::TabGroupVisualData& visuals) {
  const auto current = ReadGroup(group);
  if (!current || !tab_groups::GetTabGroupColorLabelMap().contains(
                      visuals.color())) {
    return false;
  }
  if (current->visuals != visuals) {
    model_->ChangeTabGroupVisuals(group, visuals, /*is_customized=*/true);
  }
  return true;
}

bool TabGroupSidebarAdapter::CanAddTab(tab_groups::TabGroupId group,
                                      tabs::TabInterface* tab) const {
  const auto target = ReadGroup(group);
  const auto unit = UnitForTab(tab);
  return target && !unit.empty() &&
         ScopeForTab(target->members.front().get()) == ScopeForTab(tab);
}

bool TabGroupSidebarAdapter::AddTab(tab_groups::TabGroupId group,
                                   tabs::TabInterface* tab) {
  if (!CanAddTab(group, tab)) {
    return false;
  }
  const auto unit = UnitForTab(tab);
  if (std::ranges::all_of(unit, [group](auto* member) {
        return member->GetGroup() == group;
      })) {
    return true;
  }
  const auto indices = IndicesFor(unit);
  if (indices.empty()) {
    return false;
  }
  model_->AddToExistingGroup(indices, group, /*add_to_end=*/true);
  return true;
}

bool TabGroupSidebarAdapter::CanRemoveTab(tabs::TabInterface* tab) const {
  const auto unit = UnitForTab(tab);
  return !unit.empty() && std::ranges::any_of(unit, [](auto* member) {
    return member->GetGroup().has_value();
  });
}

bool TabGroupSidebarAdapter::RemoveTab(tabs::TabInterface* tab) {
  if (!CanRemoveTab(tab)) {
    return false;
  }
  const auto indices = IndicesFor(UnitForTab(tab));
  if (indices.empty()) {
    return false;
  }
  model_->RemoveFromGroup(indices);
  return true;
}

bool TabGroupSidebarAdapter::Ungroup(tab_groups::TabGroupId group) {
  const auto current = ReadGroup(group);
  if (!current) {
    return false;
  }
  std::vector<tabs::TabInterface*> members;
  for (const auto& member : current->members) {
    if (!member || UnitForTab(member.get()).empty()) {
      return false;
    }
    members.push_back(member.get());
  }
  const auto indices = IndicesFor(members);
  if (indices.empty()) {
    return false;
  }
  model_->RemoveFromGroup(indices);
  return true;
}

void TabGroupSidebarAdapter::SetTitleEdit(
    std::optional<tab_groups::TabGroupId> group) {
  editing_group_ = group;
  if (!group) {
    ScheduleReconcile(true);
  }
}

bool TabGroupSidebarAdapter::IsTitleEditing(const base::Uuid& workspace) const {
  const auto group = editing_group_ ? ReadGroup(*editing_group_) : std::nullopt;
  return group && group->workspace_id == workspace;
}

void TabGroupSidebarAdapter::ScheduleReconcile(bool notify_presentation) {
  notify_pending_ |= notify_presentation;
  if (!model_ || scheduled_ || reconciling_) {
    return;
  }
  scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabGroupSidebarAdapter::Reconcile,
                                weak_ptr_factory_.GetWeakPtr()));
}

void TabGroupSidebarAdapter::Reconcile() {
  scheduled_ = false;
  if (!CanRead()) {
    return;
  }
  base::AutoReset<bool> guard(&reconciling_, true);
  // Readback scans the strip per native group and uses linear scope buckets
  // (normally one). Index by group/scope if actual large windows warrant it;
  // this projection has no timer or polling loop for unavailable state.
  const auto ids = model_->group_model()->ListTabGroups();
  for (const auto& id : ids) {
    std::vector<std::pair<Scope, std::vector<tabs::TabInterface*>>> buckets;
    bool coherent = true;
    for (auto* tab : *model_) {
      if (tab->GetGroup() != id) {
        continue;
      }
      const auto scope = ScopeForTab(tab);
      if (!scope || UnitForTab(tab).empty()) {
        coherent = false;
        break;
      }
      auto bucket = std::ranges::find_if(buckets, [&scope](const auto& entry) {
        return entry.first == *scope;
      });
      if (bucket == buckets.end()) {
        buckets.emplace_back(*scope, std::vector<tabs::TabInterface*>{tab});
      } else {
        bucket->second.push_back(tab);
      }
    }
    if (!coherent || buckets.size() < 2) {
      continue;
    }
    const auto visuals = *model_->group_model()->GetTabGroup(id)->visual_data();
    // Keep the first native member's scope on the original group. Each other
    // scope gets its own native group with identical presentation, without
    // opening, closing, reloading or reassigning any tab/account/partition.
    for (size_t i = 1; i < buckets.size(); ++i) {
      // Earlier buckets may have moved indices; always resolve live identities.
      const auto indices = IndicesFor(buckets[i].second);
      if (!indices.empty()) {
        const auto group = model_->AddToNewGroup(indices);
        model_->ChangeTabGroupVisuals(group, visuals);
      }
    }
  }
  // Only native group events or editor completion invalidate presentation.
  // General bridge callbacks already refresh the Sidebar. Echoing them here
  // would cause a perpetual ping-pong between adapters of two windows.
  if (std::exchange(notify_pending_, false)) {
    presentation_changed_.Run();
  }
}

void TabGroupSidebarAdapter::OnTabStripModelChanged(
    TabStripModel*, const TabStripModelChange&, const TabStripSelectionChange&) {
  ScheduleReconcile();
}
void TabGroupSidebarAdapter::TabGroupedStateChanged(
    TabStripModel*, std::optional<tab_groups::TabGroupId>,
    std::optional<tab_groups::TabGroupId>, tabs::TabInterface*, int) {
  ScheduleReconcile(true);
}
void TabGroupSidebarAdapter::OnTabGroupChanged(const TabGroupChange&) {
  ScheduleReconcile(true);
}
void TabGroupSidebarAdapter::OnSplitTabChanged(const SplitTabChange&) {
  ScheduleReconcile();
}
void TabGroupSidebarAdapter::OnTabChangedAt(tabs::TabInterface*, TabChangeType) {
  ScheduleReconcile();
}
void TabGroupSidebarAdapter::OnTabStripModelDestroyed(TabStripModel*) {
  model_ = nullptr;
  weak_ptr_factory_.InvalidateWeakPtrs();
}

}  // namespace ahoi::extensions
