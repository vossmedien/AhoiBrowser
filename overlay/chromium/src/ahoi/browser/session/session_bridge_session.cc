// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {

std::vector<base::Uuid> SessionBridge::OrderedWorkspaceIdsForSession() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<base::Uuid> workspace_ids;
  if (!workspace_service_) {
    return workspace_ids;
  }
  workspace_ids.reserve(workspace_service_->ordered_workspaces().size());
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    workspace_ids.push_back(workspace.id);
  }
  return workspace_ids;
}

std::optional<session::WindowSessionMetadata>
SessionBridge::GetWindowSessionMetadata(
    const BrowserWindowInterface* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !browser) {
    return std::nullopt;
  }
  for (const PendingWindowSessionMetadata& pending :
       pending_window_session_metadata_) {
    if (pending.browser.get() == browser) {
      return pending.metadata;
    }
  }

  const std::optional<base::Uuid> workspace_id =
      GetActiveWorkspaceForWindow(browser);
  if (!workspace_id.has_value() || !WorkspaceExists(*workspace_id)) {
    return std::nullopt;
  }
  return session::WindowSessionMetadata{.active_workspace_id = *workspace_id};
}

std::optional<session::TabSessionMetadata> SessionBridge::GetTabSessionMetadata(
    const tabs::TabInterface* tab) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return std::nullopt;
  }
  for (const PendingTabSessionMetadata& pending :
       pending_tab_session_metadata_) {
    if (pending.tab.get() == tab) {
      return pending.metadata;
    }
  }

  auto runtime_it = runtime_tabs_.find(const_cast<tabs::TabInterface*>(tab));
  if (runtime_it == runtime_tabs_.end() ||
      !runtime_it->second.workspace_id.has_value() ||
      !WorkspaceExists(*runtime_it->second.workspace_id)) {
    return std::nullopt;
  }

  const RuntimeTabState& runtime = runtime_it->second;
  bool last_active = false;
  auto browser_it = model_windows_.find(runtime.tab_strip_model);
  if (browser_it != model_windows_.end()) {
    auto window_it = windows_.find(browser_it->second);
    if (window_it != windows_.end()) {
      auto last_active_it =
          window_it->second.last_active_tabs.find(*runtime.workspace_id);
      last_active =
          last_active_it != window_it->second.last_active_tabs.end() &&
          last_active_it->second.get() == tab;
    }
  }

  return session::TabSessionMetadata{
      .workspace_id = *runtime.workspace_id,
      // Runtime node bindings are admitted only by BindTreeNodeToTab and are
      // synchronously cleared by the tree observer. Avoid a SQLite read here:
      // selection changes can run inside Chromium's tab mutation scope.
      .tree_node_id = runtime.node_id,
      .last_active_in_workspace = last_active,
  };
}

tabs::TabInterface* SessionBridge::GetLastActiveTabForWorkspace(
    const BrowserWindowInterface* browser,
    const base::Uuid& workspace_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !browser || !workspace_id.is_valid()) {
    return nullptr;
  }
  const auto window =
      windows_.find(const_cast<BrowserWindowInterface*>(browser));
  if (window == windows_.end()) {
    return nullptr;
  }
  const auto last_active = window->second.last_active_tabs.find(workspace_id);
  return last_active == window->second.last_active_tabs.end()
             ? nullptr
             : last_active->second.get();
}

bool SessionBridge::RestoreWindowSessionMetadata(
    BrowserWindowInterface* browser,
    const session::WindowSessionMetadata& metadata) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !metadata.active_workspace_id.is_valid() ||
      !ShouldTrackBrowser(browser)) {
    return false;
  }
  if (!tab_tree_ready_) {
    auto pending = std::ranges::find_if(
        pending_window_session_metadata_,
        [browser](const PendingWindowSessionMetadata& candidate) {
          return candidate.browser.get() == browser;
        });
    if (pending != pending_window_session_metadata_.end()) {
      pending->metadata = metadata;
    } else {
      pending_window_session_metadata_.push_back(
          {.browser = browser->GetWeakPtr(), .metadata = metadata});
    }
    return true;
  }
  return ApplyWindowSessionMetadataNow(browser, metadata);
}

bool SessionBridge::RestoreTabSessionMetadata(
    tabs::TabInterface* tab,
    const session::TabSessionMetadata& metadata) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (shutting_down_ || !tab || !metadata.workspace_id.is_valid() ||
      (metadata.tree_node_id.has_value() &&
       !metadata.tree_node_id->is_valid()) ||
      !ShouldTrackBrowser(browser) || tab->GetProfile() != profile_) {
    return false;
  }
  if (!tab_tree_ready_) {
    auto pending =
        std::ranges::find_if(pending_tab_session_metadata_,
                             [tab](const PendingTabSessionMetadata& candidate) {
                               return candidate.tab.get() == tab;
                             });
    if (pending != pending_tab_session_metadata_.end()) {
      pending->metadata = metadata;
    } else {
      pending_tab_session_metadata_.push_back(
          {.tab = tab->GetWeakPtr(), .metadata = metadata});
    }
    return true;
  }
  return ApplyTabSessionMetadataNow(tab, metadata);
}

void SessionBridge::ApplyPendingSessionMetadata() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_ready_ || shutting_down_) {
    return;
  }

  std::vector<PendingWindowSessionMetadata> pending_windows;
  std::vector<PendingTabSessionMetadata> pending_tabs;
  pending_windows.swap(pending_window_session_metadata_);
  pending_tabs.swap(pending_tab_session_metadata_);
  for (const PendingWindowSessionMetadata& pending : pending_windows) {
    if (pending.browser) {
      std::ignore = ApplyWindowSessionMetadataNow(pending.browser.get(),
                                                  pending.metadata);
    }
  }
  for (const PendingTabSessionMetadata& pending : pending_tabs) {
    if (pending.tab) {
      std::ignore =
          ApplyTabSessionMetadataNow(pending.tab.get(), pending.metadata);
    }
  }
}

bool SessionBridge::ApplyWindowSessionMetadataNow(
    BrowserWindowInterface* browser,
    const session::WindowSessionMetadata& metadata) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_ready_ || !ShouldTrackBrowser(browser)) {
    return false;
  }
  TrackBrowser(browser);
  auto window_it = windows_.find(browser);
  if (window_it == windows_.end()) {
    return false;
  }

  const std::vector<base::Uuid> workspace_ids = OrderedWorkspaceIdsForSession();
  const std::optional<base::Uuid> resolved =
      session::ResolveWorkspaceForRestore(metadata.active_workspace_id,
                                          workspace_ids);
  if (!resolved.has_value() || !workspace_service_->SetActiveWorkspace(
                                   window_it->second.window_id, *resolved,
                                   WorkspaceActivationSource::kRestore)) {
    return false;
  }
  PersistWindowSessionMetadata(browser);
  return true;
}

bool SessionBridge::ApplyTabSessionMetadataNow(
    tabs::TabInterface* tab,
    const session::TabSessionMetadata& metadata) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!tab_tree_ready_ || !ShouldTrackBrowser(browser) || !tab ||
      !tab->GetContents()) {
    return false;
  }
  TrackBrowser(browser);
  TrackRuntimeTab(browser->GetTabStripModel(), tab, tab->GetContents());
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end()) {
    return false;
  }

  const std::vector<base::Uuid> workspace_ids = OrderedWorkspaceIdsForSession();
  const std::optional<base::Uuid> resolved =
      session::ResolveWorkspaceForRestore(metadata.workspace_id, workspace_ids);
  if (!resolved.has_value()) {
    return false;
  }

  RuntimeTabState& runtime = runtime_it->second;
  const bool mapping_changed = runtime.workspace_id != resolved ||
                               runtime.node_id != metadata.tree_node_id;
  if (runtime.node_id.has_value()) {
    UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
  }
  RemoveTabFromLastActiveState(tab);
  runtime.workspace_id = *resolved;
  runtime.restored_session_metadata_applied = true;

  bool restored_node = false;
  if (metadata.tree_node_id.has_value() && tab_tree_store_) {
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(*metadata.tree_node_id, &node) ==
            tab_tree::TabTreeStore::Result::kOk &&
        node.workspace_id == *resolved) {
      restored_node = BindTreeNodeToTab(node, tab);
    }
  }
  if (!restored_node) {
    runtime.node_id.reset();
    runtime.workspace_id = *resolved;
  }

  RestoreLastActiveTabFlag(tab, metadata.last_active_in_workspace);
  PersistTabSessionMetadata(tab);
  // SessionRestore may call this immediately after native insertion, before
  // SessionService's own tab observer has recorded the new SessionTabHelper
  // id. Retry once after the mutation stack without blocking or relayout.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<SessionBridge> bridge,
                        base::WeakPtr<tabs::TabInterface> weak_tab) {
                       if (bridge && weak_tab) {
                         bridge->PersistTabSessionMetadata(weak_tab.get());
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr(), tab->GetWeakPtr()));
  PublishCommandItems();
  if (mapping_changed && !restored_node) {
    runtime_presentation_changed_callbacks_.Notify();
  }
  return true;
}

void SessionBridge::RemoveTabFromLastActiveState(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab) {
    return;
  }
  for (auto& window_entry : windows_) {
    auto& last_active_tabs = window_entry.second.last_active_tabs;
    for (auto last_active = last_active_tabs.begin();
         last_active != last_active_tabs.end();) {
      if (!last_active->second || last_active->second.get() == tab) {
        last_active = last_active_tabs.erase(last_active);
      } else {
        ++last_active;
      }
    }
  }
}

void SessionBridge::RestoreLastActiveTabFlag(tabs::TabInterface* tab,
                                             bool last_active) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end() ||
      !runtime_it->second.workspace_id.has_value()) {
    return;
  }
  auto browser_it = model_windows_.find(runtime_it->second.tab_strip_model);
  if (browser_it == model_windows_.end()) {
    return;
  }
  auto window_it = windows_.find(browser_it->second);
  if (window_it == windows_.end()) {
    return;
  }

  const base::Uuid workspace_id = *runtime_it->second.workspace_id;
  auto existing = window_it->second.last_active_tabs.find(workspace_id);
  if (!last_active) {
    if (existing != window_it->second.last_active_tabs.end() &&
        existing->second.get() == tab) {
      window_it->second.last_active_tabs.erase(existing);
    }
    return;
  }

  tabs::TabInterface* previous =
      existing == window_it->second.last_active_tabs.end()
          ? nullptr
          : existing->second.get();
  RemoveTabFromLastActiveState(tab);
  window_it->second.last_active_tabs.insert_or_assign(workspace_id,
                                                      tab->GetWeakPtr());
  if (previous && previous != tab) {
    PersistTabSessionMetadata(previous);
  }
}

void SessionBridge::UpdateLastActiveTab(TabStripModel* model,
                                        tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!model || !tab) {
    return;
  }
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end() ||
      runtime_it->second.tab_strip_model != model ||
      !runtime_it->second.workspace_id.has_value()) {
    return;
  }
  RestoreLastActiveTabFlag(tab, /*last_active=*/true);
  PersistTabSessionMetadata(tab);
}

void SessionBridge::PersistWindowSessionMetadata(
    BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !browser || browser->GetProfile() != profile_) {
    return;
  }
  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  const std::optional<session::WindowSessionMetadata> metadata =
      GetWindowSessionMetadata(browser);
  const std::optional<std::string> serialized =
      metadata.has_value() ? session::EncodeWindowSessionMetadata(*metadata)
                           : std::nullopt;
  const SessionID window_id = browser->GetSessionID();
  if (!session_service || !serialized.has_value() || !window_id.is_valid()) {
    return;
  }
  session_service->AddWindowExtraData(
      window_id, session::kWindowSessionMetadataExtraDataKey, *serialized);
}

void SessionBridge::PersistTabSessionMetadata(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto runtime_it = runtime_tabs_.find(tab);
  if (shutting_down_ || runtime_it == runtime_tabs_.end() ||
      !runtime_it->second.web_contents) {
    return;
  }
  auto browser_it = model_windows_.find(runtime_it->second.tab_strip_model);
  if (browser_it == model_windows_.end()) {
    return;
  }
  BrowserWindowInterface* browser = browser_it->second;
  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  const std::optional<session::TabSessionMetadata> metadata =
      GetTabSessionMetadata(tab);
  const std::optional<std::string> serialized =
      metadata.has_value() ? session::EncodeTabSessionMetadata(*metadata)
                           : std::nullopt;
  const SessionID window_id = browser->GetSessionID();
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(
      runtime_it->second.web_contents.get());
  if (!session_service || !serialized.has_value() || !window_id.is_valid() ||
      !tab_id.is_valid()) {
    return;
  }
  session_service->AddTabExtraData(
      window_id, tab_id, session::kTabSessionMetadataExtraDataKey, *serialized);
}

}  // namespace ahoi
