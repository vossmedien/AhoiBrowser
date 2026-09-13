// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/native_workspace_structure.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/resource_policy/resource_policy_service_factory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::session {
namespace {
std::optional<uint32_t> Ratio(double value) {
  if (!std::isfinite(value) || value < 0 || value > 1) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(
      std::llround(value * sync::SharedSplitRatios::kScale));
}

split_tabs::SplitTabVisualData Visuals(const sync::SharedSplitMetadata& split) {
  return split_tabs::SplitTabVisualData(
      split.topology.axis == sync::SharedSplitAxis::kHorizontal
          ? split_tabs::SplitTabLayout::kSideBySide
          : split_tabs::SplitTabLayout::kStacked,
      static_cast<double>(split.ratios.primary) /
          sync::SharedSplitRatios::kScale,
      static_cast<split_tabs::SplitTabArrangement>(split.topology.arrangement),
      static_cast<double>(split.ratios.secondary) /
          sync::SharedSplitRatios::kScale);
}
}  // namespace

std::optional<sync::SharedSplitMetadata> CaptureNativeSplit(
    SessionBridge& bridge,
    TabStripModel& model,
    split_tabs::SplitTabId native_id,
    const base::Uuid& logical_id,
    const sync::SharedSplitMetadata* previous) {
  if (!bridge.is_ready() || !logical_id.is_valid() ||
      !model.ContainsSplit(native_id)) {
    return std::nullopt;
  }
  const auto* split = model.GetSplitData(native_id);
  if (!split || !split->visual_data()) {
    return std::nullopt;
  }
  const auto tabs = split->ListTabs();
  if (tabs.size() < 2 || tabs.size() > 4) {
    return std::nullopt;
  }
  const auto& visual = *split->visual_data();
  const auto primary = Ratio(visual.split_ratio());
  const auto secondary = Ratio(visual.secondary_split_ratio());
  if (!primary || !secondary) {
    return std::nullopt;
  }
  sync::SharedSplitMetadata result;
  result.id = logical_id;
  result.topology.axis =
      visual.split_layout() == split_tabs::SplitTabLayout::kSideBySide
          ? sync::SharedSplitAxis::kHorizontal
          : sync::SharedSplitAxis::kVertical;
  result.topology.arrangement =
      tabs.size() == 3
          ? static_cast<sync::SharedSplitArrangement>(visual.arrangement())
          : sync::SharedSplitArrangement::kLinear;
  result.ratios = {.primary = *primary,
                   .secondary = tabs.size() == 2 && previous
                                    ? previous->ratios.secondary
                                    : *secondary};
  std::set<base::Uuid> identities;
  for (auto* tab : tabs) {
    if (!tab || bridge.FindTabStripModelForTab(tab) != &model) {
      return std::nullopt;
    }
    const auto node = bridge.FindSharedTreeNodeIdForTab(tab);
    const auto workspace = bridge.GetWorkspaceForTab(tab);
    if (!node || !workspace || !identities.insert(*node).second) {
      return std::nullopt;
    }
    if (!result.workspace_id.is_valid()) {
      result.workspace_id = *workspace;
    }
    if (result.workspace_id != *workspace) {
      return std::nullopt;
    }
    result.topology.member_ids.push_back(*node);
  }
  return result;
}

bool MaterializeNativeSplit(SessionBridge& bridge,
                            const sync::SharedSplitMetadata& desired,
                            std::optional<split_tabs::SplitTabId> native_id,
                            split_tabs::SplitTabId* applied_id,
                            sync::SyncAuthorization authorization) {
  if (!authorization || !authorization.Run() || !bridge.is_ready() ||
      !applied_id || !desired.id.is_valid() ||
      desired.topology.member_ids.size() < 2 ||
      desired.topology.member_ids.size() > 4 ||
      desired.ratios.primary > sync::SharedSplitRatios::kScale ||
      desired.ratios.secondary > sync::SharedSplitRatios::kScale ||
      !desired.workspace_id.is_valid() ||
      static_cast<int>(desired.topology.axis) < 0 ||
      static_cast<int>(desired.topology.axis) > 1 ||
      static_cast<int>(desired.topology.arrangement) < 0 ||
      static_cast<int>(desired.topology.arrangement) > 2 ||
      (desired.topology.member_ids.size() != 3 &&
       desired.topology.arrangement != sync::SharedSplitArrangement::kLinear)) {
    return false;
  }
  const auto bridge_lifetime = bridge.GetWeakPtrForSync();
  TabStripModel* model = nullptr;
  std::vector<base::WeakPtr<tabs::TabInterface>> members;
  std::set<base::Uuid> distinct;
  for (const auto& id : desired.topology.member_ids) {
    if (!distinct.insert(id).second) {
      return false;
    }
    tabs::TabInterface* tab = bridge.FindTabByTreeNodeId(id);
    if (!tab || bridge.FindSharedTreeNodeIdForTab(tab) != id ||
        bridge.GetWorkspaceForTab(tab) != desired.workspace_id) {
      return false;
    }
    auto* current_model = bridge.FindTabStripModelForTab(tab);
    if (!model) {
      model = current_model;
    }
    if (!model || model != current_model ||
        (tab->GetSplit() && tab->GetSplit() != native_id)) {
      return false;
    }
    if (!members.empty() && (tab->IsPinned() != members.front()->IsPinned() ||
                             tab->GetGroup() != members.front()->GetGroup())) {
      return false;
    }
    members.push_back(tab->GetWeakPtr());
  }
  const auto active = model->GetActiveTab()
                          ? model->GetActiveTab()->GetWeakPtr()
                          : base::WeakPtr<tabs::TabInterface>();
  // Passive topology/layout changes only touch resting panes. Keeping a valid
  // record pending is preferable to disrupting a form, media or visible split.
  const auto existing =
      native_id && model->ContainsSplit(*native_id)
          ? CaptureNativeSplit(bridge, *model, *native_id, desired.id, &desired)
          : std::nullopt;
  if (existing && *existing == desired) {
    *applied_id = *native_id;
    return true;
  }
  auto* resources =
      resource_policy::ResourcePolicyServiceFactory::GetForProfile(
          model->profile());
  if (!resources)
    return false;
  for (const auto& member : members) {
    if (!resources->CanArchiveTab(member.get()))
      return false;
  }
  if (native_id && model->ContainsSplit(*native_id)) {
    for (auto* member : model->GetSplitData(*native_id)->ListTabs()) {
      if (!resources->CanArchiveTab(member))
        return false;
    }
  }
  const auto visuals = Visuals(desired);
  const auto valid_model = [&] {
    return authorization.Run() && bridge_lifetime && members.front() &&
           bridge.FindTabStripModelForTab(members.front().get()) == model;
  };
  if (native_id && model->ContainsSplit(*native_id)) {
    const auto capture =
        CaptureNativeSplit(bridge, *model, *native_id, desired.id, &desired);
    if (capture && *capture == desired && valid_model()) {
      *applied_id = *native_id;
      return true;
    }
    if (capture &&
        capture->topology.member_ids == desired.topology.member_ids) {
      model->UpdateSplitLayout(*native_id, visuals.split_layout());
      if (!valid_model() || !model->ContainsSplit(*native_id)) {
        return false;
      }
      model->UpdateSplitArrangement(*native_id, visuals.arrangement());
      if (!valid_model() || !model->ContainsSplit(*native_id)) {
        return false;
      }
      model->UpdateSplitRatio(*native_id, 0, visuals.split_ratio(), false);
      if (!valid_model() || !model->ContainsSplit(*native_id)) {
        return false;
      }
      model->UpdateSplitRatio(*native_id, 1, visuals.secondary_split_ratio(),
                              false);
      if (!valid_model() || !model->ContainsSplit(*native_id)) {
        return false;
      }
      *applied_id = *native_id;
      return model->GetActiveTab() == active.get();
    }
    model->RemoveSplit(*native_id);
    if (!valid_model()) {
      return false;
    }
  }
  std::vector<int> indices;
  for (const auto& member : members) {
    if (!valid_model() || !bridge.is_operational() || !member ||
        bridge.FindTabStripModelForTab(member.get()) != model ||
        member->GetSplit()) {
      return false;
    }
    const int index = model->GetIndexOfTab(member.get());
    if (index < 0) {
      return false;
    }
    indices.push_back(index);
  }
  std::ranges::sort(indices);
  const auto id = native_id.value_or(split_tabs::SplitTabId::GenerateNew());
  *applied_id = id;
  model->RestoreSplit(id, indices, visuals);
  for (size_t index = 0; index < members.size(); ++index) {
    if (!valid_model() || !bridge.is_operational() ||
        !model->ContainsSplit(id) || !members[index]) {
      return false;
    }
    const auto current = model->GetSplitData(id)->ListTabs();
    if (current.size() != members.size()) {
      return false;
    }
    if (current[index] != members[index].get() &&
        !model->ReorderTabInSplit(members[index].get(), current[index])) {
      return false;
    }
  }
  if (!valid_model()) {
    return false;
  }
  // RestoreSplit interprets a balanced visual tuple as a request for native
  // defaults. Reapply the explicit domain ratios after membership is created.
  model->UpdateSplitRatio(id, 0, visuals.split_ratio(), false);
  if (!valid_model() || !model->ContainsSplit(id)) {
    return false;
  }
  model->UpdateSplitRatio(id, 1, visuals.secondary_split_ratio(), false);
  if (!valid_model() || !model->ContainsSplit(id)) {
    return false;
  }
  const auto capture =
      CaptureNativeSplit(bridge, *model, id, desired.id, &desired);
  if (!capture || *capture != desired ||
      model->GetActiveTab() != active.get()) {
    return false;
  }
  *applied_id = id;
  return true;
}

}  // namespace ahoi::session
