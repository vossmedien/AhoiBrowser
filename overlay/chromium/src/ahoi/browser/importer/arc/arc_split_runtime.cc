// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_runtime.h"

#include <algorithm>
#include <cmath>
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
#include "components/tabs/public/split_tab_collection.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"

namespace ahoi::importer::arc {

namespace {

std::optional<split_tabs::SplitTabVisualData> VisualDataForSplit(
    const ArcSplitDescriptor& split,
    bool* approximated_four_pane_ratio) {
  const size_t count = split.member_node_ids.size();
  if (count < 2u || count > tabs::SplitTabCollection::kMaxTabs ||
      split.normalized_ratios.size() != count) {
    return std::nullopt;
  }
  double total = 0.0;
  for (double ratio : split.normalized_ratios) {
    if (!std::isfinite(ratio) || ratio <= 0.0) {
      return std::nullopt;
    }
    total += ratio;
  }
  if (!std::isfinite(total) || total < 0.999 || total > 1.001) {
    return std::nullopt;
  }

  const split_tabs::SplitTabLayout layout =
      split.orientation == ArcSplitOrientation::kHorizontal
          ? split_tabs::SplitTabLayout::kSideBySide
          : split_tabs::SplitTabLayout::kStacked;
  if (count == 2u) {
    split_tabs::SplitTabVisualData visual(layout);
    if (!visual.set_split_ratio(split.normalized_ratios[0])) {
      return std::nullopt;
    }
    return visual;
  }
  if (count == 3u) {
    split_tabs::SplitTabVisualData visual =
        split_tabs::SplitTabVisualData::ForThreePane(
            layout, split_tabs::SplitTabArrangement::kLinear);
    const double remaining =
        split.normalized_ratios[1] + split.normalized_ratios[2];
    if (!visual.set_split_ratio(split.normalized_ratios[0]) ||
        !visual.set_secondary_split_ratio(split.normalized_ratios[1] /
                                          remaining)) {
      return std::nullopt;
    }
    return visual;
  }

  // Chromium/Ahoi renders four panes as a 2x2 grid. Arc schema 1 can encode
  // four linear factors, which the grid cannot represent exactly. Preserve
  // orientation and order with balanced ratios and report the approximation.
  *approximated_four_pane_ratio = true;
  return split_tabs::SplitTabVisualData::ForFourPane(layout);
}

bool IsValidSplitDescriptor(const ArcSplitDescriptor& split) {
  if (!split.folder_node_id.is_valid() ||
      !split.focused_member_node_id.is_valid() ||
      split.member_node_ids.size() < 2u ||
      split.member_node_ids.size() > tabs::SplitTabCollection::kMaxTabs ||
      split.normalized_ratios.size() != split.member_node_ids.size() ||
      std::ranges::find(split.member_node_ids, split.focused_member_node_id) ==
          split.member_node_ids.end()) {
    return false;
  }
  std::vector<base::Uuid> ids = split.member_node_ids;
  std::ranges::sort(ids);
  return std::ranges::adjacent_find(ids) == ids.end();
}

bool ExistingSplitMatches(
    TabStripModel* model,
    const std::vector<tabs::TabInterface*>& member_tabs,
    const split_tabs::SplitTabVisualData& expected_visual) {
  if (!model || member_tabs.empty()) {
    return false;
  }
  std::optional<split_tabs::SplitTabId> split_id;
  for (tabs::TabInterface* tab : member_tabs) {
    const int index = model->GetIndexOfTab(tab);
    const std::optional<split_tabs::SplitTabId> candidate =
        index >= 0 ? model->GetSplitForTab(index) : std::nullopt;
    if (!candidate.has_value() ||
        (split_id.has_value() && *candidate != *split_id)) {
      return false;
    }
    split_id = candidate;
  }
  const split_tabs::SplitTabData* data =
      split_id ? model->GetSplitData(*split_id) : nullptr;
  return data && data->ListTabs() == member_tabs && data->visual_data() &&
         *data->visual_data() == expected_visual;
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

ArcSplitRuntimeResult ReconstructArcSplits(BrowserWindowInterface* browser,
                                           SessionBridge* session_bridge,
                                           const ArcImportPlan& applied_plan) {
  ArcSplitRuntimeResult result;
  if (!browser || !session_bridge || !session_bridge->is_ready() ||
      applied_plan.schema_version != kArcImportPlanSchemaVersion) {
    return result;
  }

  std::map<base::Uuid, tab_tree::TreeNode> nodes;
  for (const tab_tree::TreeNode& node : applied_plan.tree.nodes) {
    nodes.emplace(node.id, node);
  }
  std::set<base::Uuid> claimed_member_ids;
  TabStripModel* const model = browser->GetTabStripModel();
  if (!model) {
    return result;
  }
  for (const ArcSplitDescriptor& split : applied_plan.splits) {
    if (!IsValidSplitDescriptor(split)) {
      return result;
    }
    bool approximated = false;
    if (!VisualDataForSplit(split, &approximated).has_value()) {
      return result;
    }
    for (const base::Uuid& node_id : split.member_node_ids) {
      const auto node_it = nodes.find(node_id);
      if (node_it == nodes.end() || node_it->second.tombstone ||
          node_it->second.type != tab_tree::TreeNodeType::kSavedPage ||
          !node_it->second.url.is_valid() ||
          !node_it->second.url.SchemeIsHTTPOrHTTPS() ||
          node_it->second.url.has_username() ||
          node_it->second.url.has_password() ||
          !claimed_member_ids.insert(node_id).second) {
        return result;
      }
    }

    // Existing bindings can be present after Chromium restored a split from a
    // crash between the durable tree commit and the importer journal rename.
    // An existing partial/different split is rejected before opening anything.
    std::vector<tabs::TabInterface*> existing_tabs;
    bool any_existing_split = false;
    bool all_members_exist = true;
    for (const base::Uuid& node_id : split.member_node_ids) {
      tabs::TabInterface* tab = session_bridge->FindTabByTreeNodeId(node_id);
      if (!tab) {
        all_members_exist = false;
        continue;
      }
      if (tab->GetBrowserWindowInterface() != browser) {
        return result;
      }
      const int index = model->GetIndexOfTab(tab);
      if (index < 0) {
        return result;
      }
      any_existing_split |= model->GetSplitForTab(index).has_value();
      existing_tabs.push_back(tab);
    }
    if (!existing_tabs.empty() && !any_existing_split) {
      return result;
    }
    if (any_existing_split) {
      bool ignored_approximation = false;
      const std::optional<split_tabs::SplitTabVisualData> expected_visual =
          VisualDataForSplit(split, &ignored_approximation);
      if (!all_members_exist ||
          existing_tabs.size() != split.member_node_ids.size() ||
          !expected_visual ||
          !ExistingSplitMatches(model, existing_tabs, *expected_visual)) {
        return result;
      }
    }
  }

  for (const ArcSplitDescriptor& split : applied_plan.splits) {
    std::vector<tabs::TabInterface*> member_tabs;
    for (const base::Uuid& node_id : split.member_node_ids) {
      const tab_tree::TreeNode& node = nodes.at(node_id);
      tabs::TabInterface* existing =
          session_bridge->FindTabByTreeNodeId(node_id);
      if (existing) {
        member_tabs.push_back(existing);
        continue;
      }
      std::unique_ptr<content::WebContents> contents =
          content::WebContents::Create(
              content::WebContents::CreateParams(browser->GetProfile()));
      content::WebContents* const raw_contents = contents.get();
      // Appending first runs BrowserTabStripModelDelegate::WillAddWebContents,
      // which attaches Chromium's normal per-tab helpers. Loading directly
      // afterwards avoids depending on the broad //chrome/browser/ui target.
      model->AppendWebContents(std::move(contents), /*foreground=*/false);
      raw_contents->GetController().LoadURL(node.url, content::Referrer(),
                                            ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                            std::string());
      tabs::TabInterface* tab =
          session_bridge->FindTabByWebContents(raw_contents);
      if (!tab || tab->GetBrowserWindowInterface() != browser ||
          !session_bridge->BindTreeNodeToTab(node, tab)) {
        if (tab) {
          tab->Close();
        }
        CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
        result.opened_tabs.clear();
        return result;
      }
      result.opened_tabs.push_back(tab->GetWeakPtr());
      member_tabs.push_back(tab);
    }

    std::vector<int> indices;
    for (tabs::TabInterface* tab : member_tabs) {
      const int index = model->GetIndexOfTab(tab);
      if (index < 0) {
        CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
        result.opened_tabs.clear();
        return result;
      }
      indices.push_back(index);
    }
    const int active_index = indices.front();
    model->ActivateTabAt(active_index);
    indices.erase(indices.begin());
    std::ranges::sort(indices);

    bool approximated = false;
    std::optional<split_tabs::SplitTabVisualData> visual =
        VisualDataForSplit(split, &approximated);
    if (!visual.has_value()) {
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    if (ExistingSplitMatches(model, member_tabs, *visual)) {
      const auto focus_it = std::ranges::find(split.member_node_ids,
                                              split.focused_member_node_id);
      const size_t focus_offset =
          static_cast<size_t>(focus_it - split.member_node_ids.begin());
      model->ActivateTabAt(model->GetIndexOfTab(member_tabs[focus_offset]));
      ++result.reconstructed_split_count;
      if (approximated) {
        ++result.approximated_four_pane_ratio_count;
      }
      continue;
    }
    const split_tabs::SplitTabId split_id =
        model->AddToNewSplit(std::move(indices), std::move(*visual),
                             split_tabs::SplitTabCreatedSource::kExtensionsApi);
    const split_tabs::SplitTabData* split_data = model->GetSplitData(split_id);
    if (!split_data || split_data->ListTabs().size() != member_tabs.size()) {
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    const auto focus_it =
        std::ranges::find(split.member_node_ids, split.focused_member_node_id);
    const size_t focus_offset =
        static_cast<size_t>(focus_it - split.member_node_ids.begin());
    const int focus_index = model->GetIndexOfTab(member_tabs[focus_offset]);
    if (focus_index < 0) {
      CloseArcImportRuntimeTabs(std::move(result.opened_tabs));
      result.opened_tabs.clear();
      return result;
    }
    model->ActivateTabAt(focus_index);
    ++result.reconstructed_split_count;
    if (approximated) {
      ++result.approximated_four_pane_ratio_count;
    }
  }

  result.status = ArcImportStatus::kOk;
  return result;
}

}  // namespace ahoi::importer::arc
