// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 step 2 (handoff 052): converting a Workspace of the main Profile
// into a fully separated Workspace. Source side: ask, export, hand off,
// delete after success. Receiving side: import, reopen, report.

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/portable_workspace_structure.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_internal.h"
#include "ahoi/browser/session/session_prefs.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi {

namespace {

// The receiving Profile's structure controller may still be settling its
// first commit; the import is retried briefly instead of failing.
constexpr int kConversionImportAttempts = 40;
constexpr base::TimeDelta kConversionImportRetry = base::Milliseconds(250);

}  // namespace

void SessionBridge::ConvertWorkspaceToIsolated(
    const base::Uuid& workspace_id,
    base::OnceCallback<void(WorkspaceConversionResult)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool known = workspace_service_ &&
                     std::ranges::any_of(
                         workspace_service_->ordered_workspaces(),
                         [&workspace_id](const tab_tree::Workspace& workspace) {
                           return workspace.id == workspace_id;
                         });
  // The main Profile keeps at least one Workspace, and a separated Profile
  // has exactly one, so neither side of a conversion can empty a Profile.
  if (shutting_down_ || !tab_tree_ready_ || !tab_tree_store_ || !known ||
      workspace_service_->ordered_workspaces().size() < 2 ||
      FindIsolatedProfileEntry() || workspace_conversion_running_ ||
      workspace_deletion_close_) {
    std::move(done).Run(WorkspaceConversionResult::kUnavailable);
    return;
  }
  std::vector<content::WebContents*> pages;
  std::vector<GURL> reopen_urls;
  for (const auto& [tab, runtime] : runtime_tabs_) {
    content::WebContents* contents = runtime.web_contents.get();
    if (runtime.workspace_id != workspace_id || !contents) {
      continue;
    }
    pages.push_back(contents);
    // Saved pages move as rows of the tree; an open temporary page has no
    // row and comes along as a new page. Sessions do not move with it.
    if (runtime.is_temporary) {
      const GURL url = session_internal::GetRuntimeTabUrl(contents);
      if (url.is_valid() && !url.IsAboutBlank()) {
        reopen_urls.push_back(url);
      }
    }
  }
  workspace_conversion_running_ = true;
  if (pages.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SessionBridge::OnConversionPagesAnswered,
                                  weak_ptr_factory_.GetWeakPtr(), workspace_id,
                                  std::move(reopen_urls), std::move(done),
                                  /*all_agreed=*/true));
    return;
  }
  workspace_conversion_close_ = session::GroupPageClose::Ask(
      std::move(pages),
      base::BindOnce(&SessionBridge::OnConversionPagesAnswered,
                     weak_ptr_factory_.GetWeakPtr(), workspace_id,
                     std::move(reopen_urls), std::move(done)));
}

void SessionBridge::OnConversionPagesAnswered(
    base::Uuid workspace_id,
    std::vector<GURL> reopen_urls,
    base::OnceCallback<void(WorkspaceConversionResult)> done,
    bool all_agreed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto fail = [this](WorkspaceConversionResult result,
                           base::OnceCallback<void(WorkspaceConversionResult)>
                               done) {
    workspace_conversion_close_.reset();
    workspace_conversion_running_ = false;
    std::move(done).Run(result);
  };
  if (!all_agreed || shutting_down_) {
    // A veto leaves the Workspace, its pages and every store unchanged.
    fail(WorkspaceConversionResult::kCancelled, std::move(done));
    return;
  }
  tab_tree::TabTreeSnapshot snapshot;
  const std::optional<std::string> encoded_state =
      tab_tree_store_ ? tab_tree_store_->ReadWorkspaceStructureState()
                      : std::nullopt;
  if (!ExportTabTreeSnapshot(&snapshot) || !encoded_state) {
    fail(WorkspaceConversionResult::kFailed, std::move(done));
    return;
  }
  session::WorkspaceStructureState structure;
  if (!encoded_state->empty()) {
    std::optional<session::WorkspaceStructureState> decoded =
        session::DecodeWorkspaceStructureState(*encoded_state);
    if (!decoded) {
      fail(WorkspaceConversionResult::kFailed, std::move(done));
      return;
    }
    structure = std::move(*decoded);
  }
  std::optional<session::PortableWorkspaceStructure> chosen =
      session::SelectPortableWorkspaceStructure(
          snapshot, structure, {workspace_id},
          /*include_temporary_pages=*/false, /*include_archives=*/true);
  if (!chosen || chosen->tree.workspaces.size() != 1 ||
      chosen->tree.workspaces.front().id != workspace_id) {
    fail(WorkspaceConversionResult::kFailed, std::move(done));
    return;
  }
  const tab_tree::PortableWorkspace source = chosen->tree.workspaces.front();
  // The new Profile seeds this Workspace from its registry entry (name, icon,
  // accent) with the bootstrap key "0" and the default archive policy. The
  // imported record must be identical to that seed, or the import is a
  // conflict; the policy is applied after the import, and the process-wide
  // position is the registry entry's `sort_key`.
  chosen->tree.workspaces.front().sort_key = "0";
  chosen->tree.workspaces.front().archive_policy =
      tab_tree::Workspace().archive_policy;

  session::PendingWorkspaceConversion pending;
  pending.structure = std::move(*chosen);
  pending.archive_policy = source.archive_policy;
  pending.reopen_urls = std::move(reopen_urls);
  pending.done = base::BindOnce(&SessionBridge::OnWorkspaceConverted,
                                weak_ptr_factory_.GetWeakPtr(), workspace_id,
                                std::move(done));
  session::ConvertToIsolatedWorkspace(
      session::IsolatedProfileEntry{.workspace_id = workspace_id,
                                    .name = source.name,
                                    .icon = source.icon,
                                    .accent_argb = source.accent_argb,
                                    .sort_key = source.sort_key},
      std::move(pending));
}

void SessionBridge::OnWorkspaceConverted(
    base::Uuid workspace_id,
    base::OnceCallback<void(WorkspaceConversionResult)> done,
    bool imported) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::unique_ptr<session::GroupPageClose> group =
      std::move(workspace_conversion_close_);
  workspace_conversion_running_ = false;
  if (!imported || shutting_down_) {
    // The new Profile deletes itself; the source stays as it was.
    std::move(done).Run(WorkspaceConversionResult::kFailed);
    return;
  }
  const tab_tree::TabTreeStore::Result result = CommitWorkspaceDeletion(
      workspace_id,
      session::FindWebsiteSessionBinding(profile_->GetPrefs(), workspace_id));
  if (result != tab_tree::TabTreeStore::Result::kOk) {
    LOG(ERROR) << "Ahoi converted a Workspace but could not remove its source";
    std::move(done).Run(WorkspaceConversionResult::kFailed);
    return;
  }
  // The agreed pages close without a second question.
  if (group) {
    group->ClosePages();
  }
  std::move(done).Run(WorkspaceConversionResult::kConverted);
}

void SessionBridge::ContinueWorkspaceConversion(
    const std::string& profile_dir) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<session::PendingWorkspaceConversion> pending =
      session::TakePendingWorkspaceConversion(profile_dir);
  if (!pending) {
    // Interrupted by a restart before the import finished: the source still
    // exists in the main Profile, so this half-converted Profile goes.
    LOG(WARNING) << "Ahoi drops an interrupted Workspace conversion";
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<SessionBridge> bridge) {
                         if (bridge) {
                           session::DeleteIsolatedWorkspaceProfile(
                               bridge->profile_, base::DoNothing());
                         }
                       },
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  ImportConvertedWorkspace(std::move(*pending), /*attempt=*/0);
}

void SessionBridge::ImportConvertedWorkspace(
    session::PendingWorkspaceConversion pending,
    int attempt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const session::PortableWorkspaceStructure structure = pending.structure;
  CommitPortableWorkspaceImport(
      structure, base::BindRepeating([] { return true; }),
      base::BindOnce(&SessionBridge::OnConvertedWorkspaceImported,
                     weak_ptr_factory_.GetWeakPtr(), std::move(pending),
                     attempt));
}

void SessionBridge::OnConvertedWorkspaceImported(
    session::PendingWorkspaceConversion pending,
    int attempt,
    PortableImportResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result == PortableImportResult::kUnavailable &&
      attempt + 1 < kConversionImportAttempts && !shutting_down_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SessionBridge::ImportConvertedWorkspace,
                       weak_ptr_factory_.GetWeakPtr(), std::move(pending),
                       attempt + 1),
        kConversionImportRetry);
    return;
  }
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  const std::optional<session::IsolatedProfileEntry> entry =
      FindIsolatedProfileEntry();
  const bool imported = result == PortableImportResult::kImported ||
                        result == PortableImportResult::kNoChanges;
  if (!imported || !entry || !local_state) {
    LOG(ERROR) << "Ahoi could not import a converted Workspace: "
               << static_cast<int>(result);
    std::move(pending.done).Run(false);
    session::DeleteIsolatedWorkspaceProfile(profile_, base::DoNothing());
    return;
  }
  std::ignore =
      SetWorkspaceArchivePolicy(entry->workspace_id, pending.archive_policy);
  session::SetIsolatedProfileState(local_state, entry->profile_dir,
                                   session::IsolatedProfileState::kActive);
  local_state->CommitPendingWrite();
  ScheduleTabTreePersistence();
  if (ProfileBrowserCollection* browsers =
          ProfileBrowserCollection::GetForProfile(profile_)) {
    BrowserWindowInterface* target = nullptr;
    browsers->ForEach(
        [&target](BrowserWindowInterface* browser) {
          if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
            target = browser;
            return false;
          }
          return true;
        },
        BrowserCollection::Order::kActivation);
    for (const GURL& url : pending.reopen_urls) {
      if (target) {
        target->OpenGURL(url, WindowOpenDisposition::NEW_BACKGROUND_TAB);
      }
    }
  }
  std::move(pending.done).Run(true);
}

}  // namespace ahoi
