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

namespace {

GURL SanitizeCommandUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return GURL();
  }
  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.ClearPassword();
  return url.ReplaceComponents(replacements);
}

std::u16string SanitizeCommandTitle(std::u16string title,
                                    const GURL& original_url,
                                    const GURL& sanitized_url) {
  if ((original_url.has_username() || original_url.has_password()) &&
      title == base::UTF8ToUTF16(original_url.spec())) {
    return base::UTF8ToUTF16(sanitized_url.spec());
  }
  return title;
}

}  // namespace

void SessionBridge::TrackRuntimeTab(TabStripModel* model,
                                    tabs::TabInterface* tab,
                                    content::WebContents* contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !model || !tab || !contents ||
      !model_windows_.contains(model) || tab->GetProfile() != profile_) {
    return;
  }

  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it != runtime_tabs_.end() &&
      runtime_it->second.tab.get() != tab) {
    // A detached tab may be destroyed before its posted retirement task. If a
    // later allocation reuses the same address first, discard the expired
    // state rather than attaching it to the unrelated new TabInterface.
    RemoveRuntimeTab(tab);
    runtime_it = runtime_tabs_.end();
  }
  if (runtime_it != runtime_tabs_.end()) {
    if (runtime_it->second.tab_strip_model != model) {
      RemoveTabFromLastActiveState(tab);
    }
    runtime_it->second.tab_strip_model = model;
    if (runtime_it->second.web_contents.get() != contents) {
      UpdateRuntimeTabContents(tab, runtime_it->second.web_contents.get(),
                               contents);
    } else {
      contents_tabs_.insert_or_assign(contents, runtime_it->second.tab);
    }
    ScheduleTreeNodeBinding(tab);
    return;
  }

  auto contents_it = contents_tabs_.find(contents);
  if (contents_it != contents_tabs_.end()) {
    tabs::TabInterface* existing_tab = contents_it->second.get();
    if (existing_tab && existing_tab != tab) {
      return;
    }
    contents_tabs_.erase(contents_it);
  }

  auto [inserted_it, inserted] = runtime_tabs_.try_emplace(tab);
  if (!inserted) {
    return;
  }
  RuntimeTabState& runtime = inserted_it->second;
  runtime.tab_strip_model = model;
  runtime.tab = tab->GetWeakPtr();
  runtime.web_contents = contents->GetWeakPtr();
  contents_tabs_.emplace(contents, runtime.tab);
  runtime.will_discard_contents_subscription =
      tab->RegisterWillDiscardContents(base::BindRepeating(
          &SessionBridge::OnTabWillDiscardContents, base::Unretained(this)));
  runtime.will_detach_subscription =
      tab->RegisterWillDetach(base::BindRepeating(
          &SessionBridge::OnTabWillDetach, base::Unretained(this)));
  runtime.did_insert_subscription = tab->RegisterDidInsert(base::BindRepeating(
      &SessionBridge::OnTabDidInsert, base::Unretained(this)));
  if (TabUIHelper* tab_ui_helper = TabUIHelper::From(tab)) {
    runtime.tab_ui_change_subscription =
        tab_ui_helper->AddTabUIChangeCallback(base::BindRepeating(
            &SessionBridge::OnTabUIChanged, base::Unretained(this), tab));
  }
  ScheduleTreeNodeBinding(tab);
  const GURL current_url = session_internal::GetRuntimeTabUrl(contents);
  runtime.last_observed_url = current_url;
  runtime.last_observed_title =
      session_internal::GetRuntimeTabTitle(tab, current_url);
}

void SessionBridge::RemoveRuntimeTab(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end()) {
    return;
  }
  RuntimeTabState& runtime = runtime_it->second;
  RemoveTabFromLastActiveState(tab);
  if (runtime.node_id.has_value()) {
    auto node_it = node_tabs_.find(*runtime.node_id);
    if (node_it != node_tabs_.end() &&
        (!node_it->second || node_it->second.get() == tab)) {
      node_tabs_.erase(node_it);
    }
  }
  for (auto contents_it = contents_tabs_.begin();
       contents_it != contents_tabs_.end();) {
    tabs::TabInterface* mapped_tab = contents_it->second.get();
    if (!mapped_tab || mapped_tab == tab) {
      contents_it = contents_tabs_.erase(contents_it);
    } else {
      ++contents_it;
    }
  }
  runtime_tabs_.erase(runtime_it);
  PublishCommandItems();
}

void SessionBridge::EnsureTreeNodeForTab(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_store_ || !workspace_service_ || !tab) {
    return;
  }

  auto runtime_it = runtime_tabs_.find(tab);
  if (runtime_it == runtime_tabs_.end() ||
      runtime_it->second.node_id.has_value() ||
      !runtime_it->second.tab_strip_model || !runtime_it->second.web_contents) {
    return;
  }
  TabStripModel* model = runtime_it->second.tab_strip_model;
  auto window_it = model_windows_.find(model);
  if (window_it == model_windows_.end()) {
    return;
  }

  // A restored tab already carries its persisted workspace. Never replace it
  // with the currently active workspace merely because URL matching runs on a
  // later task after native insertion.
  std::optional<base::Uuid> workspace_id = runtime_it->second.workspace_id;
  if (!workspace_id.has_value() || !WorkspaceExists(*workspace_id)) {
    workspace_id = GetActiveWorkspaceForWindow(window_it->second);
  }
  if (!workspace_id.has_value() &&
      !workspace_service_->ordered_workspaces().empty()) {
    workspace_id = workspace_service_->ordered_workspaces().front().id;
  }
  if (!workspace_id.has_value()) {
    return;
  }
  if (runtime_it->second.workspace_id != workspace_id) {
    RemoveTabFromLastActiveState(tab);
    runtime_it->second.workspace_id = *workspace_id;
  }
  if (tab->IsActivated()) {
    UpdateLastActiveTab(model, tab);
  }

  // A missing tree-node id in restored metadata is meaningful: the tab was
  // temporary. Do not silently convert it into a saved tab just because its
  // URL also exists in the durable tree.
  if (runtime_it->second.restored_session_metadata_applied) {
    PersistTabSessionMetadata(tab);
    return;
  }

  content::WebContents* contents = runtime_it->second.web_contents.get();
  const GURL url = session_internal::GetRuntimeTabUrl(contents);
  // New-tab pages are intentionally fungible runtime surfaces. Rebinding a
  // freshly created one to an older saved "New Tab" node hides the first
  // user-created temporary tab from the list below the saved tree.
  if (url == GURL(chrome::kChromeUINewTabURL) ||
      url == GURL(chrome::kChromeUINewTabPageURL)) {
    PersistTabSessionMetadata(tab);
    return;
  }
  std::vector<tab_tree::TreeNode> matching_nodes;
  if (tab_tree_store_->FindSavedPagesByUrl(*workspace_id, url,
                                           &matching_nodes) !=
      tab_tree::TabTreeStore::Result::kOk) {
    PersistTabSessionMetadata(tab);
    return;
  }
  for (const tab_tree::TreeNode& node : matching_nodes) {
    if (!FindTabByTreeNodeId(node.id) && BindTreeNodeToTab(node, tab)) {
      PublishCommandItems();
      return;
    }
  }
  PersistTabSessionMetadata(tab);
}

void SessionBridge::ScheduleTreeNodeBinding(tabs::TabInterface* tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab) {
    return;
  }
  // Tab insertion runs inside Chromium's no-blocking mutation scope. The
  // durable tree currently uses an in-memory SQLite index, so even a read must
  // be deferred until that mutation stack has unwound. A newly opened tab is
  // intentionally left temporary when no matching saved page exists.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<SessionBridge> bridge,
                        base::WeakPtr<tabs::TabInterface> weak_tab) {
                       if (bridge && weak_tab) {
                         bridge->EnsureTreeNodeForTab(weak_tab.get());
                       }
                     },
                     weak_ptr_factory_.GetWeakPtr(), tab->GetWeakPtr()));
}

void SessionBridge::PublishCommandItems() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !command_service_ || !workspace_service_) {
    return;
  }

  std::vector<CommandItem> open_tabs;
  open_tabs.reserve(runtime_tabs_.size());
  for (const auto& entry : runtime_tabs_) {
    const RuntimeTabState& runtime = entry.second;
    tabs::TabInterface* tab = runtime.tab.get();
    content::WebContents* contents = runtime.web_contents.get();
    if (!tab || !contents) {
      continue;
    }
    const GURL original_url = session_internal::GetRuntimeTabUrl(contents);
    const GURL url = SanitizeCommandUrl(original_url);
    if (!url.is_valid() || url.is_empty()) {
      continue;
    }
    open_tabs.push_back({
        .type = CommandItemType::kOpenTab,
        .stable_id =
            runtime.node_id.has_value()
                ? runtime.node_id->AsLowercaseString()
                : base::StrCat({"runtime:", base::NumberToString(
                                                tab->GetHandle().raw_value())}),
        .title = SanitizeCommandTitle(
            session_internal::GetRuntimeTabTitle(tab, original_url),
            original_url, url),
        .secondary_text = base::UTF8ToUTF16(url.spec()),
        .keywords = {base::UTF8ToUTF16(url.spec())},
        .url = url,
        .priority = tab->IsActivated() ? 300 : 200,
        .last_used = tab->IsActivated() ? base::Time::Now() : base::Time(),
        .sleeping = contents->WasDiscarded(),
    });
  }
  CHECK(command_service_->ReplaceItems(CommandItemType::kOpenTab,
                                       std::move(open_tabs)));

  // Saved pages remain command-bar destinations even when no live Chromium tab
  // is bound to them. Walk the durable tree iteratively so unlimited nesting
  // does not translate into an unbounded C++ call stack.
  struct PendingParent {
    base::Uuid workspace_id;
    std::optional<base::Uuid> parent_id;
    std::u16string path;
  };
  std::vector<CommandItem> saved_pages;
  std::vector<CommandItem> folders;
  std::vector<PendingParent> pending_parents;
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    pending_parents.push_back({.workspace_id = workspace.id,
                               .parent_id = std::nullopt,
                               .path = workspace.name});
  }
  bool tree_read_succeeded = true;
  while (!pending_parents.empty() && tree_read_succeeded) {
    PendingParent pending = std::move(pending_parents.back());
    pending_parents.pop_back();
    std::vector<tab_tree::TreeNode> children;
    if (tab_tree_store_->GetChildren(pending.workspace_id, pending.parent_id,
                                     &children) !=
        tab_tree::TabTreeStore::Result::kOk) {
      tree_read_succeeded = false;
      break;
    }
    for (const tab_tree::TreeNode& node : children) {
      std::u16string node_path = pending.path;
      if (!node_path.empty()) {
        node_path.append(u" / ");
      }
      node_path.append(node.title);
      if (node.type == tab_tree::TreeNodeType::kFolder) {
        folders.push_back({
            .type = CommandItemType::kFolder,
            .stable_id = node.id.AsLowercaseString(),
            .title = node.title,
            .secondary_text = node_path,
            .keywords = {node_path, pending.path},
            .priority = 110,
            .last_used = node.modified_at,
        });
        pending_parents.push_back({.workspace_id = pending.workspace_id,
                                   .parent_id = node.id,
                                   .path = std::move(node_path)});
        continue;
      }
      // A running saved page is already represented by the open-tab item
      // above using the same durable UUID. Publishing it again as a saved-page
      // item creates two visually identical Cmd+T results for one tab.
      if (FindTabByTreeNodeId(node.id)) {
        continue;
      }
      if (!node.url.is_valid() || node.url.is_empty()) {
        continue;
      }
      const GURL sanitized_url = SanitizeCommandUrl(node.url);
      if (!sanitized_url.is_valid() || sanitized_url.is_empty()) {
        continue;
      }
      const std::u16string url = base::UTF8ToUTF16(sanitized_url.spec());
      const std::u16string title =
          SanitizeCommandTitle(node.title, node.url, sanitized_url);
      std::u16string safe_node_path = pending.path;
      if (!safe_node_path.empty()) {
        safe_node_path.append(u" / ");
      }
      safe_node_path.append(title);
      saved_pages.push_back({
          .type = CommandItemType::kSavedPage,
          .stable_id = node.id.AsLowercaseString(),
          .title = title,
          .secondary_text = url,
          .keywords = {url, std::move(safe_node_path)},
          .url = sanitized_url,
          .priority = 120,
          .last_used = node.modified_at,
      });
    }
  }
  if (tree_read_succeeded) {
    CHECK(command_service_->ReplaceItems(CommandItemType::kSavedPage,
                                         std::move(saved_pages)));
    CHECK(command_service_->ReplaceItems(CommandItemType::kFolder,
                                         std::move(folders)));
  } else {
    command_service_->ClearItems(CommandItemType::kSavedPage);
    command_service_->ClearItems(CommandItemType::kFolder);
  }

  std::vector<CommandItem> workspaces;
  workspaces.reserve(workspace_service_->ordered_workspaces().size());
  for (const tab_tree::Workspace& workspace :
       workspace_service_->ordered_workspaces()) {
    workspaces.push_back({
        .type = CommandItemType::kWorkspace,
        .stable_id = workspace.id.AsLowercaseString(),
        .title = workspace.name,
        .priority = 100,
    });
  }
  CHECK(command_service_->ReplaceItems(CommandItemType::kWorkspace,
                                       std::move(workspaces)));
}

}  // namespace ahoi
