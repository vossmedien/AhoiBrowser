// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_runtime.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"

namespace ahoi::importer::arc {

namespace {

using NodeMap = std::map<base::Uuid, const tab_tree::TreeNode*>;

NodeMap BuildNodeMap(const ArcImportPlan& plan) {
  NodeMap nodes;
  for (const tab_tree::TreeNode& node : plan.tree.nodes) {
    nodes.emplace(node.id, &node);
  }
  return nodes;
}

bool HasReconstructableMissingMemberUrls(const ArcImportPlan& plan,
                                         const NodeMap& nodes,
                                         SessionBridge* session_bridge) {
  for (const ArcSplitDescriptor& split : plan.splits) {
    for (const base::Uuid& node_id : split.member_node_ids) {
      if (session_bridge->FindTabByTreeNodeId(node_id)) {
        continue;
      }
      const auto node_it = nodes.find(node_id);
      if (node_it == nodes.end() || !node_it->second->url.is_valid() ||
          !node_it->second->url.SchemeIsHTTPOrHTTPS() ||
          node_it->second->url.has_username() ||
          node_it->second->url.has_password()) {
        return false;
      }
    }
  }
  return true;
}

struct RuntimeSplitObservation {
  ArcSplitVerification verification = ArcSplitVerification::kUnavailable;
  std::vector<tabs::TabInterface*> member_tabs;
};

RuntimeSplitObservation InspectRuntimeSplit(
    BrowserWindowInterface* browser,
    SessionBridge* session_bridge,
    TabStripModel* model,
    const ArcSplitDescriptor& split,
    const split_tabs::SplitTabVisualData& expected_visual) {
  RuntimeSplitObservation observation;
  bool has_missing_member = false;
  bool has_split_member = false;
  bool has_unsplit_member = false;
  std::optional<split_tabs::SplitTabId> observed_split_id;
  std::set<tabs::TabInterface*> observed_tabs;

  for (const base::Uuid& node_id : split.member_node_ids) {
    tabs::TabInterface* const tab =
        session_bridge->FindTabByTreeNodeId(node_id);
    if (!tab) {
      has_missing_member = true;
      continue;
    }
    if (!observed_tabs.insert(tab).second ||
        tab->GetBrowserWindowInterface() != browser) {
      observation.verification = ArcSplitVerification::kConflict;
      return observation;
    }
    const int index = model->GetIndexOfTab(tab);
    if (index < 0) {
      observation.verification = ArcSplitVerification::kConflict;
      return observation;
    }
    const std::optional<split_tabs::SplitTabId> split_id =
        model->GetSplitForTab(index);
    if (!split_id.has_value()) {
      has_unsplit_member = true;
    } else {
      has_split_member = true;
      if ((observed_split_id.has_value() && *observed_split_id != *split_id) ||
          split_id->is_empty()) {
        observation.verification = ArcSplitVerification::kConflict;
        return observation;
      }
      observed_split_id = split_id;
    }
    observation.member_tabs.push_back(tab);
  }

  if (!has_split_member) {
    observation.verification = ArcSplitVerification::kRepairableMissing;
    return observation;
  }
  if (has_missing_member || has_unsplit_member ||
      observation.member_tabs.size() != split.member_node_ids.size() ||
      !observed_split_id.has_value()) {
    observation.verification = ArcSplitVerification::kConflict;
    return observation;
  }

  const split_tabs::SplitTabData* const split_data =
      model->GetSplitData(*observed_split_id);
  if (!split_data || split_data->ListTabs() != observation.member_tabs ||
      !split_data->visual_data() ||
      *split_data->visual_data() != expected_visual) {
    observation.verification = ArcSplitVerification::kConflict;
    return observation;
  }
  observation.verification = ArcSplitVerification::kExact;
  return observation;
}

void RollBackCreatedSplits(
    TabStripModel* model,
    const std::vector<split_tabs::SplitTabId>& created_split_ids) {
  if (!model) {
    return;
  }
  for (auto it = created_split_ids.rbegin(); it != created_split_ids.rend();
       ++it) {
    if (model->ContainsSplit(*it)) {
      model->RemoveSplit(*it);
    }
  }
}

bool ReorderCreatedSplit(
    TabStripModel* model,
    split_tabs::SplitTabId split_id,
    const std::vector<tabs::TabInterface*>& expected_tabs) {
  for (size_t position = 0; position < expected_tabs.size(); ++position) {
    const split_tabs::SplitTabData* const split_data =
        model->GetSplitData(split_id);
    if (!split_data || split_data->ListTabs().size() != expected_tabs.size()) {
      return false;
    }
    const std::vector<tabs::TabInterface*>& current_tabs =
        split_data->ListTabs();
    if (current_tabs[position] == expected_tabs[position]) {
      continue;
    }
    if (!model->ReorderTabInSplit(expected_tabs[position],
                                  current_tabs[position])) {
      return false;
    }
  }
  return true;
}

bool ActivateExpectedFocus(BrowserWindowInterface* browser,
                           TabStripModel* model,
                           SessionBridge* session_bridge,
                           const ArcSplitDescriptor& split) {
  tabs::TabInterface* const focused_tab =
      session_bridge->FindTabByTreeNodeId(split.focused_member_node_id);
  if (!browser || browser->IsDeleteScheduled() || !focused_tab ||
      focused_tab->GetBrowserWindowInterface() != browser ||
      model->GetIndexOfTab(focused_tab) < 0) {
    return false;
  }
  model->ActivateTab(focused_tab);
  return model->GetActiveTab() == focused_tab;
}

}  // namespace

void CloseArcImportRuntimeTabs(
    std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs) {
  for (auto it = opened_tabs.rbegin(); it != opened_tabs.rend(); ++it) {
    if (*it) {
      (*it)->Close();
    }
  }
}

ArcSplitVerification VerifyArcSplitRuntime(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan,
                                           bool require_focus) {
  if (!browser || browser->IsDeleteScheduled() || !session_bridge ||
      !session_bridge->is_ready()) {
    return ArcSplitVerification::kUnavailable;
  }
  if (!IsValidArcSplitStructure(applied_plan)) {
    return ArcSplitVerification::kConflict;
  }
  TabStripModel* const model = browser->GetTabStripModel();
  if (!model) {
    return ArcSplitVerification::kUnavailable;
  }
  const NodeMap nodes = BuildNodeMap(applied_plan);
  if (!HasReconstructableMissingMemberUrls(applied_plan, nodes,
                                           session_bridge)) {
    return ArcSplitVerification::kConflict;
  }

  ArcSplitVerification aggregate = ArcSplitVerification::kExact;
  for (const ArcSplitDescriptor& split : applied_plan.splits) {
    const ArcSplitVisualExpectation visual =
        *BuildArcSplitVisualExpectation(split);
    const RuntimeSplitObservation observation = InspectRuntimeSplit(
        browser, session_bridge, model, split, visual.visual_data);
    if (observation.verification == ArcSplitVerification::kUnavailable ||
        observation.verification == ArcSplitVerification::kConflict) {
      return observation.verification;
    }
    if (observation.verification == ArcSplitVerification::kRepairableMissing) {
      aggregate = ArcSplitVerification::kRepairableMissing;
    }
  }
  if (require_focus && aggregate == ArcSplitVerification::kExact) {
    if (applied_plan.splits.empty()) {
      return ArcSplitVerification::kConflict;
    }
    tabs::TabInterface* const expected_focus =
        session_bridge->FindTabByTreeNodeId(
            applied_plan.splits.back().focused_member_node_id);
    if (!expected_focus ||
        expected_focus->GetBrowserWindowInterface() != browser ||
        model->GetActiveTab() != expected_focus) {
      return ArcSplitVerification::kRepairableMissing;
    }
  }
  return aggregate;
}

ArcSplitRuntimeResult ReconstructArcSplits(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan) {
  ArcSplitRuntimeResult result;
  TabStripModel* const model = browser ? browser->GetTabStripModel() : nullptr;
  const ArcSplitVerification initial =
      VerifyArcSplitRuntime(browser, session_bridge, applied_plan);
  if (initial == ArcSplitVerification::kExact) {
    if (!applied_plan.splits.empty() &&
        !ActivateExpectedFocus(browser, model, session_bridge,
                               applied_plan.splits.back())) {
      return result;
    }
    if (!applied_plan.splits.empty() &&
        VerifyArcSplitRuntime(browser, session_bridge, applied_plan,
                              /*require_focus=*/true) !=
            ArcSplitVerification::kExact) {
      return result;
    }
    result.status = ArcImportStatus::kOk;
    result.reconstructed_split_count = applied_plan.splits.size();
    result.approximated_four_pane_ratio_count =
        static_cast<size_t>(std::ranges::count_if(
            applied_plan.splits, [](const ArcSplitDescriptor& split) {
              return BuildArcSplitVisualExpectation(split)
                  ->approximated_four_pane_ratios;
            }));
    return result;
  }
  if (initial != ArcSplitVerification::kRepairableMissing) {
    return result;
  }

  const NodeMap nodes = BuildNodeMap(applied_plan);
  std::map<base::Uuid, tabs::TabInterface*> runtime_tabs;

  // Materialize every missing member before creating any native split. This
  // keeps URL/binding failures ahead of split mutation and lets the verifier
  // reject a concurrent foreign split before the first AddToNewSplit call.
  for (const ArcSplitDescriptor& split : applied_plan.splits) {
    for (const base::Uuid& node_id : split.member_node_ids) {
      if (runtime_tabs.contains(node_id)) {
        continue;
      }
      tabs::TabInterface* existing =
          session_bridge->FindTabByTreeNodeId(node_id);
      if (existing) {
        runtime_tabs.emplace(node_id, existing);
        continue;
      }

      const tab_tree::TreeNode& node = *nodes.at(node_id);
      std::unique_ptr<content::WebContents> contents =
          content::WebContents::Create(
              content::WebContents::CreateParams(browser->GetProfile()));
      content::WebContents* const raw_contents = contents.get();
      // Appending first runs BrowserTabStripModelDelegate::WillAddWebContents,
      // which attaches Chromium's normal per-tab helpers.
      model->AppendWebContents(std::move(contents), /*foreground=*/false);
      raw_contents->GetController().LoadURL(node.url, content::Referrer(),
                                            ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                            std::string());
      tabs::TabInterface* const tab =
          session_bridge->FindTabByWebContents(raw_contents);
      if (!tab || tab->GetBrowserWindowInterface() != browser ||
          !session_bridge->BindTreeNodeToTab(node, tab)) {
        if (tab) {
          tab->Close();
        } else {
          const int appended_index = model->GetIndexOfWebContents(raw_contents);
          if (appended_index >= 0) {
            model->CloseWebContentsAt(appended_index, /*close_types=*/0);
          }
        }
        CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
        result.opened_tabs.clear();
        return result;
      }
      result.opened_tabs.push_back(tab->GetWeakPtr());
      runtime_tabs.emplace(node_id, tab);
    }
  }

  if (VerifyArcSplitRuntime(browser, session_bridge, applied_plan) !=
      ArcSplitVerification::kRepairableMissing) {
    CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
    result.opened_tabs.clear();
    return result;
  }

  std::vector<split_tabs::SplitTabId> created_split_ids;
  for (const ArcSplitDescriptor& split : applied_plan.splits) {
    const ArcSplitVisualExpectation visual =
        *BuildArcSplitVisualExpectation(split);
    RuntimeSplitObservation observation = InspectRuntimeSplit(
        browser, session_bridge, model, split, visual.visual_data);
    if (observation.verification == ArcSplitVerification::kExact) {
      if (!ActivateExpectedFocus(browser, model, session_bridge, split)) {
        RollBackCreatedSplits(model, created_split_ids);
        CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
        result.opened_tabs.clear();
        return result;
      }
      ++result.reconstructed_split_count;
      if (visual.approximated_four_pane_ratios) {
        ++result.approximated_four_pane_ratio_count;
      }
      continue;
    }
    if (observation.verification != ArcSplitVerification::kRepairableMissing ||
        observation.member_tabs.size() != split.member_node_ids.size()) {
      RollBackCreatedSplits(model, created_split_ids);
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }

    tabs::TabInterface* const leading_tab = observation.member_tabs.front();
    model->ActivateTab(leading_tab);
    const int leading_index = model->GetIndexOfTab(leading_tab);
    if (leading_index < 0 || model->GetSplitForTab(leading_index).has_value()) {
      RollBackCreatedSplits(model, created_split_ids);
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    std::vector<int> remaining_indices;
    for (size_t index = 1; index < observation.member_tabs.size(); ++index) {
      const int tab_index =
          model->GetIndexOfTab(observation.member_tabs[index]);
      if (tab_index < 0 || model->GetSplitForTab(tab_index).has_value()) {
        RollBackCreatedSplits(model, created_split_ids);
        CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
        result.opened_tabs.clear();
        return result;
      }
      remaining_indices.push_back(tab_index);
    }
    std::ranges::sort(remaining_indices);
    const split_tabs::SplitTabId split_id =
        model->AddToNewSplit(std::move(remaining_indices), visual.visual_data,
                             split_tabs::SplitTabCreatedSource::kExtensionsApi);
    created_split_ids.push_back(split_id);
    if (!ReorderCreatedSplit(model, split_id, observation.member_tabs)) {
      RollBackCreatedSplits(model, created_split_ids);
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    const RuntimeSplitObservation exact = InspectRuntimeSplit(
        browser, session_bridge, model, split, visual.visual_data);
    if (exact.verification != ArcSplitVerification::kExact) {
      RollBackCreatedSplits(model, created_split_ids);
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }

    if (!ActivateExpectedFocus(browser, model, session_bridge, split)) {
      RollBackCreatedSplits(model, created_split_ids);
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    ++result.reconstructed_split_count;
    if (visual.approximated_four_pane_ratios) {
      ++result.approximated_four_pane_ratio_count;
    }
  }

  if (VerifyArcSplitRuntime(browser, session_bridge, applied_plan,
                            /*require_focus=*/true) !=
      ArcSplitVerification::kExact) {
    RollBackCreatedSplits(model, created_split_ids);
    CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
    result.opened_tabs.clear();
    result.reconstructed_split_count = 0;
    result.approximated_four_pane_ratio_count = 0;
    return result;
  }
  result.status = ArcImportStatus::kOk;
  return result;
}

}  // namespace ahoi::importer::arc
