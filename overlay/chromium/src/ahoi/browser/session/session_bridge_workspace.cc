// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_internal.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

std::optional<base::Uuid> SessionBridge::GetWindowId(
    const BrowserWindowInterface* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !browser) {
    return std::nullopt;
  }
  auto it = windows_.find(const_cast<BrowserWindowInterface*>(browser));
  return it == windows_.end() ? std::nullopt
                              : std::make_optional(it->second.window_id);
}

BrowserWindowInterface* SessionBridge::FindWindowById(
    const base::Uuid& window_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !window_id.is_valid()) {
    return nullptr;
  }
  auto it = id_windows_.find(window_id);
  return it == id_windows_.end() ? nullptr : it->second.get();
}

bool SessionBridge::SetActiveWorkspaceForWindow(
    BrowserWindowInterface* browser,
    const base::Uuid& workspace_id,
    WorkspaceActivationSource source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !workspace_service_) {
    return false;
  }
  const std::optional<base::Uuid> window_id = GetWindowId(browser);
  return window_id.has_value() && workspace_service_->SetActiveWorkspace(
                                      *window_id, workspace_id, source);
}

std::optional<base::Uuid> SessionBridge::ActivateRelativeWorkspaceForWindow(
    BrowserWindowInterface* browser,
    int delta,
    WorkspaceActivationSource source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !workspace_service_ || delta == 0) {
    return std::nullopt;
  }
  const std::optional<base::Uuid> window_id = GetWindowId(browser);
  return window_id.has_value()
             ? workspace_service_->ActivateRelative(*window_id, delta,
                                                    /*wrap=*/true, source)
             : std::nullopt;
}

std::optional<base::Uuid> SessionBridge::GetActiveWorkspaceForWindow(
    const BrowserWindowInterface* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !workspace_service_) {
    return std::nullopt;
  }
  const std::optional<base::Uuid> window_id = GetWindowId(browser);
  return window_id.has_value()
             ? workspace_service_->GetActiveWorkspace(*window_id)
             : std::nullopt;
}

std::optional<base::Uuid> SessionBridge::CreateWorkspace(
    std::u16string name,
    std::u16string icon,
    std::optional<uint32_t> accent_argb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_ || name.empty()) {
    return std::nullopt;
  }

  const base::Time now = base::Time::Now();
  tab_tree::Workspace workspace{
      .id = base::Uuid::GenerateRandomV4(),
      .name = std::move(name),
      .icon = std::move(icon),
      .sort_key =
          workspace_service_->ordered_workspaces().empty()
              ? std::string("00000000")
              : workspace_service_->ordered_workspaces().back().sort_key + '@',
      .accent_argb = accent_argb,
      .created_at = now,
      .modified_at = now,
  };
  if (tab_tree_store_->CreateWorkspace(workspace) !=
          tab_tree::TabTreeStore::Result::kOk ||
      !RefreshWorkspaceSnapshot()) {
    return std::nullopt;
  }
  return workspace.id;
}

std::optional<base::Uuid> SessionBridge::DuplicateWorkspace(
    const base::Uuid& source_workspace_id,
    std::u16string name,
    std::u16string icon,
    std::optional<uint32_t> accent_argb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_ || !source_workspace_id.is_valid() || name.empty()) {
    return std::nullopt;
  }

  tab_tree::Workspace source_workspace;
  if (tab_tree_store_->GetWorkspace(source_workspace_id, &source_workspace) !=
          tab_tree::TabTreeStore::Result::kOk ||
      source_workspace.tombstone) {
    return std::nullopt;
  }

  const auto& ordered_workspaces = workspace_service_->ordered_workspaces();
  const auto source_it = std::ranges::find_if(
      ordered_workspaces,
      [&source_workspace_id](const tab_tree::Workspace& workspace) {
        return workspace.id == source_workspace_id;
      });
  if (source_it == ordered_workspaces.end()) {
    return std::nullopt;
  }

  std::string duplicate_sort_key = source_workspace.sort_key + '@';
  while (std::ranges::any_of(
      ordered_workspaces,
      [&duplicate_sort_key](const tab_tree::Workspace& workspace) {
        return workspace.sort_key == duplicate_sort_key;
      })) {
    duplicate_sort_key.push_back('@');
  }

  const base::Time now = base::Time::Now();
  tab_tree::Workspace duplicate{
      .model_version = source_workspace.model_version,
      .id = base::Uuid::GenerateRandomV4(),
      .name = std::move(name),
      .icon = std::move(icon),
      .sort_key = std::move(duplicate_sort_key),
      .accent_argb = accent_argb,
      .created_at = now,
      .modified_at = now,
  };
  if (tab_tree_store_->DuplicateWorkspace(source_workspace_id, duplicate) !=
          tab_tree::TabTreeStore::Result::kOk ||
      !RefreshWorkspaceSnapshot()) {
    return std::nullopt;
  }
  return duplicate.id;
}

tab_tree::TabTreeStore::Result SessionBridge::UpdateWorkspacePresentation(
    const base::Uuid& workspace_id,
    std::u16string name,
    std::u16string icon,
    std::optional<uint32_t> accent_argb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_) {
    return tab_tree::TabTreeStore::Result::kNotInitialized;
  }
  const tab_tree::TabTreeStore::Result result =
      tab_tree_store_->UpdateWorkspacePresentation(
          workspace_id, std::move(name), std::move(icon), accent_argb,
          base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }
  return RefreshWorkspaceSnapshot()
             ? tab_tree::TabTreeStore::Result::kOk
             : tab_tree::TabTreeStore::Result::kDatabaseError;
}

tab_tree::TabTreeStore::Result SessionBridge::DeleteWorkspace(
    const base::Uuid& workspace_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ ||
      !workspace_service_) {
    return tab_tree::TabTreeStore::Result::kNotInitialized;
  }
  if (workspace_service_->ordered_workspaces().size() <= 1) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }
  auto fallback = std::ranges::find_if(
      workspace_service_->ordered_workspaces(),
      [&workspace_id](const tab_tree::Workspace& candidate) {
        return candidate.id != workspace_id;
      });
  if (fallback == workspace_service_->ordered_workspaces().end()) {
    return tab_tree::TabTreeStore::Result::kInvalidArgument;
  }

  const tab_tree::TabTreeStore::Result result =
      tab_tree_store_->DeleteWorkspace(workspace_id, base::Time::Now());
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }

  bool runtime_changed = false;
  for (auto& [tab, runtime] : runtime_tabs_) {
    if (runtime.workspace_id != workspace_id) {
      continue;
    }
    UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
    RemoveTabFromLastActiveState(tab);
    runtime.workspace_id = fallback->id;
    if (tab->IsActivated() && runtime.tab_strip_model) {
      UpdateLastActiveTab(runtime.tab_strip_model, tab);
    }
    PersistTabSessionMetadata(tab);
    runtime_changed = true;
  }
  if (!RefreshWorkspaceSnapshot()) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }
  if (runtime_changed) {
    runtime_presentation_changed_callbacks_.Notify();
  }
  return tab_tree::TabTreeStore::Result::kOk;
}

bool SessionBridge::BindTreeNodeToTab(const tab_tree::TreeNode& node,
                                      tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab ||
      node.model_version != tab_tree::kCurrentModelVersion ||
      !node.id.is_valid() || !node.workspace_id.is_valid() ||
      (node.parent_id.has_value() && !node.parent_id->is_valid()) ||
      node.type != tab_tree::TreeNodeType::kSavedPage || node.tombstone ||
      node.title.empty() || node.sort_key.empty() ||
      node.created_at.is_null() || node.modified_at.is_null() ||
      !node.url.is_valid() || node.url.is_empty() ||
      !WorkspaceExists(node.workspace_id)) {
    return false;
  }

  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end() ||
      !runtime_it->second.tab_strip_model || !runtime_it->second.web_contents) {
    return false;
  }
  auto existing_node_it = node_tabs_.find(node.id);
  if (existing_node_it != node_tabs_.end()) {
    tabs::TabInterface* existing_tab = existing_node_it->second.get();
    if (existing_tab && existing_tab != tab) {
      return false;
    }
    if (!existing_tab) {
      node_tabs_.erase(existing_node_it);
    }
  }

  RuntimeTabState& runtime = runtime_it->second;
  if (runtime.node_id.has_value() && runtime.node_id != node.id) {
    auto old_node_it = node_tabs_.find(*runtime.node_id);
    if (old_node_it != node_tabs_.end() &&
        (!old_node_it->second || old_node_it->second.get() == tab)) {
      node_tabs_.erase(old_node_it);
    }
  }
  if (runtime.workspace_id != node.workspace_id) {
    RemoveTabFromLastActiveState(tab);
  }
  runtime.node_id = node.id;
  runtime.workspace_id = node.workspace_id;
  node_tabs_.insert_or_assign(node.id, runtime.tab);
  if (tab->IsActivated()) {
    UpdateLastActiveTab(runtime.tab_strip_model, tab);
  }
  PersistTabSessionMetadata(tab);
  PublishCommandItems();
  runtime_presentation_changed_callbacks_.Notify();
  return true;
}

std::optional<base::Uuid> SessionBridge::SaveTabAtWorkspaceRoot(
    BrowserWindowInterface* browser,
    tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ || !browser ||
      !tab) {
    return std::nullopt;
  }

  auto runtime_it = runtime_tabs_.find(tab);
  auto window_it = windows_.find(browser);
  if (runtime_it == runtime_tabs_.end() || window_it == windows_.end() ||
      runtime_it->second.tab_strip_model != window_it->second.tab_strip_model ||
      !runtime_it->second.web_contents) {
    return std::nullopt;
  }
  if (runtime_it->second.node_id.has_value()) {
    return runtime_it->second.node_id;
  }

  const std::optional<base::Uuid> workspace_id =
      GetActiveWorkspaceForWindow(browser);
  if (!workspace_id.has_value()) {
    return std::nullopt;
  }

  std::vector<tab_tree::TreeNode> root_nodes;
  if (tab_tree_store_->GetChildren(*workspace_id, std::nullopt, &root_nodes) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return std::nullopt;
  }

  content::WebContents* contents = runtime_it->second.web_contents.get();
  const GURL url = session_internal::GetRuntimeTabUrl(contents);
  const base::Time now = base::Time::Now();
  tab_tree::TreeNode node{
      .id = base::Uuid::GenerateRandomV4(),
      .workspace_id = *workspace_id,
      .type = tab_tree::TreeNodeType::kSavedPage,
      .title = session_internal::GetRuntimeTabTitle(tab, url),
      .url = url,
      .sort_key = root_nodes.empty() ? std::string(1, '@')
                                     : root_nodes.back().sort_key + '@',
      .created_at = now,
      .modified_at = now,
  };
  if (tab_tree_store_->CreateNode(node) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return std::nullopt;
  }
  if (!BindTreeNodeToTab(node, tab)) {
    std::ignore = tab_tree_store_->DeleteNode(node.id, base::Time::Now());
    return std::nullopt;
  }
  runtime_it->second.workspace_id = *workspace_id;
  return node.id;
}

void SessionBridge::UnbindTreeNodeFromTab(tabs::TabInterface* tab) {
  UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/true);
}

void SessionBridge::MakeTabTemporary(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
  PublishCommandItems();
}

void SessionBridge::UnbindTreeNodeFromTabInternal(tabs::TabInterface* tab,
                                                  bool clear_workspace) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab) {
    return;
  }
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end()) {
    return;
  }
  RuntimeTabState& runtime = runtime_it->second;
  const bool presentation_changed =
      runtime.node_id.has_value() ||
      (clear_workspace && runtime.workspace_id.has_value());
  if (runtime.node_id.has_value()) {
    auto node_it = node_tabs_.find(*runtime.node_id);
    if (node_it != node_tabs_.end() &&
        (!node_it->second || node_it->second.get() == tab)) {
      node_tabs_.erase(node_it);
    }
  }
  runtime.node_id.reset();
  if (clear_workspace) {
    RemoveTabFromLastActiveState(tab);
    runtime.workspace_id.reset();
  }
  PersistTabSessionMetadata(tab);
  if (presentation_changed) {
    runtime_presentation_changed_callbacks_.Notify();
  }
}

tabs::TabInterface* SessionBridge::FindTabByTreeNodeId(
    const base::Uuid& node_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !node_id.is_valid()) {
    return nullptr;
  }
  auto it = node_tabs_.find(node_id);
  return it == node_tabs_.end() ? nullptr : it->second.get();
}

tabs::TabInterface* SessionBridge::FindTabForOpenTabStableId(
    std::string_view stable_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || stable_id.empty()) {
    return nullptr;
  }
  constexpr std::string_view kRuntimePrefix = "runtime:";
  if (stable_id.starts_with(kRuntimePrefix)) {
    int runtime_handle = 0;
    if (!base::StringToInt(stable_id.substr(kRuntimePrefix.size()),
                           &runtime_handle)) {
      return nullptr;
    }
    for (const auto& [tab, runtime] : runtime_tabs_) {
      tabs::TabInterface* candidate = runtime.tab.get();
      if (candidate && candidate->GetHandle().raw_value() == runtime_handle) {
        return candidate;
      }
    }
    return nullptr;
  }
  const base::Uuid node_id = base::Uuid::ParseLowercase(stable_id);
  return node_id.is_valid() ? FindTabByTreeNodeId(node_id) : nullptr;
}

std::optional<base::Uuid> SessionBridge::FindTreeNodeIdForTab(
    const tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return std::nullopt;
  }
  auto it = runtime_tabs_.find(const_cast<tabs::TabInterface*>(tab));
  return it == runtime_tabs_.end() ? std::nullopt : it->second.node_id;
}

bool SessionBridge::HasLiveTabsInWorkspace(
    const base::Uuid& workspace_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::ranges::any_of(runtime_tabs_, [&](const auto& entry) {
    const auto& runtime = entry.second;
    return runtime.tab && runtime.workspace_id == workspace_id;
  });
}

std::optional<base::Uuid> SessionBridge::GetWorkspaceForTab(
    const tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return std::nullopt;
  }
  auto it = runtime_tabs_.find(const_cast<tabs::TabInterface*>(tab));
  return it == runtime_tabs_.end() ? std::nullopt : it->second.workspace_id;
}

tabs::TabInterface* SessionBridge::FindTabByWebContents(
    const content::WebContents* contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !contents) {
    return nullptr;
  }
  auto it = contents_tabs_.find(const_cast<content::WebContents*>(contents));
  return it == contents_tabs_.end() ? nullptr : it->second.get();
}

TabStripModel* SessionBridge::FindTabStripModelForTab(
    const tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return nullptr;
  }
  auto it = runtime_tabs_.find(const_cast<tabs::TabInterface*>(tab));
  return it == runtime_tabs_.end() ? nullptr : it->second.tab_strip_model.get();
}

content::WebContents* SessionBridge::FindWebContentsForTab(
    const tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return nullptr;
  }
  auto it = runtime_tabs_.find(const_cast<tabs::TabInterface*>(tab));
  return it == runtime_tabs_.end() ? nullptr : it->second.web_contents.get();
}

}  // namespace ahoi
