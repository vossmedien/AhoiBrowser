// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/navigation/command_service.h"
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
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"

namespace ahoi {

SessionBridge::SessionBridge(Profile* profile,
                             WorkspaceService* workspace_service,
                             CommandService* command_service)
    : profile_(profile),
      workspace_service_(workspace_service),
      command_service_(command_service) {
  if (!profile_ || !workspace_service_ || !command_service_ ||
      profile_->IsOffTheRecord() || !profile_->IsRegularProfile() ||
      !profile_->AllowsBrowserWindows()) {
    shutting_down_ = true;
    return;
  }

  if (!InitializeTabTree()) {
    shutting_down_ = true;
    return;
  }
  if (!session::RegisterWorkspaceSessionMetadataProvider(profile_, this)) {
    shutting_down_ = true;
    tab_tree_store_.reset();
    return;
  }
  session_metadata_provider_registered_ = true;
  BeginTabTreeLoad();
}

SessionBridge::~SessionBridge() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

base::WeakPtr<sync::ProfileSyncUiBridge> SessionBridge::GetWeakPtrForSync() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return weak_ptr_factory_.GetWeakPtr();
}

void SessionBridge::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (session_metadata_provider_registered_) {
    session::UnregisterWorkspaceSessionMetadataProvider(profile_, this);
    session_metadata_provider_registered_ = false;
  }
  if (shutting_down_) {
    return;
  }

  persistence_timer_.Stop();
  if (tab_tree_ready_ && persistence_enabled_ && tab_tree_store_) {
    PersistTabTreeNow();
  }
  shutting_down_ = true;
  tab_tree_ready_ = false;
  workspace_reconciliation_scheduled_ = false;
  weak_ptr_factory_.InvalidateWeakPtrs();

  browser_collection_observation_.Reset();
  if (workspace_service_) {
    workspace_service_->RemoveObserver(this);
  }
  TabStripModelObserver::StopObservingAll(this);
  if (tab_tree_store_) {
    tab_tree_store_->RemoveObserver(this);
  }

  if (workspace_service_) {
    for (const auto& [browser, window] : windows_) {
      workspace_service_->ForgetWindow(window.window_id);
    }
  }
  contents_tabs_.clear();
  node_tabs_.clear();
  runtime_tabs_.clear();
  pending_tab_session_metadata_.clear();
  pending_window_session_metadata_.clear();
  model_windows_.clear();
  id_windows_.clear();
  windows_.clear();
  profile_ = nullptr;
  workspace_service_ = nullptr;
  command_service_ = nullptr;
  tab_tree_store_.reset();
  if (ready_callback_for_testing_) {
    std::move(ready_callback_for_testing_).Run();
  }
}

bool SessionBridge::InitializeTabTree() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto store = std::make_unique<tab_tree::TabTreeStore>();
  if (!store->InitializeInMemory()) {
    return false;
  }

  const base::Time now = base::Time::Now();
  tab_tree::Workspace workspace;
  workspace.id = base::Uuid::GenerateRandomV4();
  workspace.name = u"Ahoi";
  workspace.icon = u"A";
  workspace.sort_key = "00000000";
  workspace.accent_argb = 0xff43d1bd;
  workspace.created_at = now;
  workspace.modified_at = now;
  if (store->CreateWorkspace(workspace) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return false;
  }
  if (!workspace_service_->ReplaceWorkspaces({workspace})) {
    return false;
  }

  tab_tree_store_ = std::move(store);
  tab_tree_database_path_ =
      profile_->GetPath().AppendASCII(kTabTreeDatabaseFilename);
  persistence_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  return true;
}

void SessionBridge::BeginTabTreeLoad() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(persistence_task_runner_);
  persistence_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SessionBridge::LoadTabTreeSnapshot,
                     tab_tree_database_path_),
      base::BindOnce(&SessionBridge::OnTabTreeLoaded,
                     weak_ptr_factory_.GetWeakPtr()));
}

void SessionBridge::OnTabTreeLoaded(TabTreeLoadResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_ || !tab_tree_store_) {
    return;
  }

  if (result.status == TabTreeLoadStatus::kLoaded) {
    std::vector<tab_tree::Workspace> active_workspaces;
    if (tab_tree_store_->ReplaceWithSnapshot(result.snapshot) !=
            tab_tree::TabTreeStore::Result::kOk ||
        tab_tree_store_->GetWorkspaces(&active_workspaces) !=
            tab_tree::TabTreeStore::Result::kOk ||
        active_workspaces.empty() ||
        !workspace_service_->ReplaceWorkspaces(active_workspaces)) {
      LOG(ERROR) << "Ahoi tab-tree snapshot could not be restored";
      persistence_enabled_ = false;
    }
  } else if (result.status == TabTreeLoadStatus::kFailed) {
    // Keep the browser usable but never overwrite a database that failed
    // validation. Recovery/import UI can make that decision explicitly.
    LOG(ERROR) << "Ahoi tab-tree database could not be loaded";
    persistence_enabled_ = false;
  }

  tab_tree_ready_ = true;
  if (!FinishRuntimeInitialization()) {
    shutting_down_ = true;
    tab_tree_ready_ = false;
  } else if (result.status == TabTreeLoadStatus::kMissing) {
    ScheduleTabTreePersistence();
  }

  if (ready_callback_for_testing_) {
    std::move(ready_callback_for_testing_).Run();
  }
}

bool SessionBridge::FinishRuntimeInitialization() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProfileBrowserCollection* browser_collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  if (!browser_collection) {
    return false;
  }

  tab_tree_store_->AddObserver(this);
  workspace_service_->AddObserver(this);
  browser_collection_observation_.Observe(browser_collection);
  browser_collection->ForEach(
      [this](BrowserWindowInterface* browser) {
        TrackBrowser(browser);
        return true;
      },
      BrowserCollection::Order::kCreation,
      /*enumerate_new_browsers=*/true);
  ReconcileWorkspaces();
  // SessionRestore can create windows and tabs before the asynchronous Ahoi
  // tree snapshot has loaded. Apply their metadata only now, against the
  // authoritative workspace/node identities rather than the bootstrap store.
  ApplyPendingSessionMetadata();
  // The sidebar may already be showing its immediate bootstrap projection.
  // Publish one authoritative ready transition even when restoration did not
  // otherwise mutate a tracked tab, so saved/runtime classification cannot
  // remain stuck on the bootstrap store.
  runtime_presentation_changed_callbacks_.Notify();
  PublishCommandItems();
  NotifyTabTreeSnapshotChanged();
  return true;
}

void SessionBridge::ScheduleTabTreePersistence() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_ready_ || !persistence_enabled_ || shutting_down_) {
    return;
  }
  persistence_timer_.Start(FROM_HERE, base::Milliseconds(250),
                           base::BindOnce(&SessionBridge::PersistTabTreeNow,
                                          weak_ptr_factory_.GetWeakPtr()));
}

void SessionBridge::PersistTabTreeNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_store_ || !persistence_enabled_ || !persistence_task_runner_) {
    return;
  }
  tab_tree::TabTreeSnapshot snapshot;
  if (tab_tree_store_->ExportSnapshot(&snapshot) !=
      tab_tree::TabTreeStore::Result::kOk) {
    LOG(ERROR) << "Ahoi tab-tree snapshot could not be exported";
    return;
  }
  persistence_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath path, tab_tree::TabTreeSnapshot snapshot) {
            std::ignore = SessionBridge::PersistTabTreeSnapshot(
                path, std::move(snapshot));
          },
          tab_tree_database_path_, std::move(snapshot)));
}

void SessionBridge::NotifyTabTreeSnapshotChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_tree::TabTreeSnapshot snapshot;
  if (ExportTabTreeSnapshot(&snapshot)) {
    tab_tree_snapshot_changed_callbacks_.Notify(snapshot);
  }
}

// static
SessionBridge::TabTreeLoadResult SessionBridge::LoadTabTreeSnapshot(
    const base::FilePath& path) {
  if (!base::PathExists(path)) {
    return {.status = TabTreeLoadStatus::kMissing};
  }
  tab_tree::TabTreeStore store;
  tab_tree::TabTreeSnapshot snapshot;
  if (!store.Initialize(path) ||
      store.ExportSnapshot(&snapshot) != tab_tree::TabTreeStore::Result::kOk) {
    return {.status = TabTreeLoadStatus::kFailed};
  }
  return {.status = TabTreeLoadStatus::kLoaded,
          .snapshot = std::move(snapshot)};
}

// static
bool SessionBridge::PersistTabTreeSnapshot(const base::FilePath& path,
                                           tab_tree::TabTreeSnapshot snapshot) {
  tab_tree::TabTreeStore store;
  return store.Initialize(path) && store.ReplaceWithSnapshot(snapshot) ==
                                       tab_tree::TabTreeStore::Result::kOk;
}

void SessionBridge::RunWhenReadyForTesting(base::OnceClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(callback);
  if (tab_tree_ready_ || shutting_down_) {
    std::move(callback).Run();
    return;
  }
  CHECK(!ready_callback_for_testing_);
  ready_callback_for_testing_ = std::move(callback);
}

void SessionBridge::FlushPersistenceForTesting(base::OnceClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(callback);
  FlushPersistenceForBackup(base::BindOnce(
      [](base::OnceClosure done, bool) { std::move(done).Run(); },
      std::move(callback)));
}

void SessionBridge::FlushPersistenceForBackup(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(callback);
  persistence_timer_.Stop();
  if (!tab_tree_store_ || !persistence_enabled_ || !persistence_task_runner_) {
    std::move(callback).Run(false);
    return;
  }
  tab_tree::TabTreeSnapshot snapshot;
  if (tab_tree_store_->ExportSnapshot(&snapshot) !=
      tab_tree::TabTreeStore::Result::kOk) {
    std::move(callback).Run(false);
    return;
  }
  persistence_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SessionBridge::PersistTabTreeSnapshot,
                     tab_tree_database_path_, std::move(snapshot)),
      std::move(callback));
}

base::CallbackListSubscription
SessionBridge::AddRuntimePresentationChangedCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return runtime_presentation_changed_callbacks_.Add(std::move(callback));
}

void SessionBridge::RequestLocalTabCapture() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  // Every BrowserSidebarHostView is subscribed to this profile-scoped list.
  // Its refresh reads the current TabStripModel before publishing, so an
  // opt-in can never repopulate sync from a cache retained while disabled.
  runtime_presentation_changed_callbacks_.Notify();
}

base::CallbackListSubscription SessionBridge::AddTabTreeSnapshotChangedCallback(
    base::RepeatingCallback<void(const tab_tree::TabTreeSnapshot&)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(callback);
  base::RepeatingCallback<void(const tab_tree::TabTreeSnapshot&)> initial =
      callback;
  auto subscription =
      tab_tree_snapshot_changed_callbacks_.Add(std::move(callback));
  tab_tree::TabTreeSnapshot snapshot;
  if (ExportTabTreeSnapshot(&snapshot)) {
    initial.Run(snapshot);
  }
  return subscription;
}

bool SessionBridge::ExportTabTreeSnapshot(tab_tree::TabTreeSnapshot* snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return snapshot && tab_tree_ready_ && !shutting_down_ && tab_tree_store_ &&
         tab_tree_store_->ExportSnapshot(snapshot) ==
             tab_tree::TabTreeStore::Result::kOk;
}

tab_tree::TabTreeStore::Result SessionBridge::ApplySyncedTabTreeSnapshot(
    tab_tree::TabTreeSnapshot snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_ready_ || shutting_down_ || !tab_tree_store_) {
    return tab_tree::TabTreeStore::Result::kNotInitialized;
  }
  tab_tree::TabTreeSnapshot current;
  if (tab_tree_store_->ExportSnapshot(&current) !=
      tab_tree::TabTreeStore::Result::kOk) {
    return tab_tree::TabTreeStore::Result::kDatabaseError;
  }
  snapshot.undo_operations = current.undo_operations;
  if (snapshot == current) {
    return tab_tree::TabTreeStore::Result::kOk;
  }
  const tab_tree::TabTreeStore::Result result =
      tab_tree_store_->ReplaceWithSnapshot(snapshot);
  if (result != tab_tree::TabTreeStore::Result::kOk ||
      !RefreshWorkspaceSnapshot()) {
    return result == tab_tree::TabTreeStore::Result::kOk
               ? tab_tree::TabTreeStore::Result::kDatabaseError
               : result;
  }
  for (auto it = node_tabs_.begin(); it != node_tabs_.end();) {
    tabs::TabInterface* tab = it->second.get();
    if (!tab) {
      it = node_tabs_.erase(it);
      continue;
    }
    tab_tree::TreeNode node;
    if (tab_tree_store_->GetNode(it->first, &node) !=
            tab_tree::TabTreeStore::Result::kOk ||
        node.tombstone) {
      UnbindTreeNodeFromTabInternal(tab, /*clear_workspace=*/false);
      it = node_tabs_.begin();
      continue;
    }
    auto runtime = runtime_tabs_.find(tab);
    if (runtime != runtime_tabs_.end()) {
      runtime->second.workspace_id = node.workspace_id;
      PersistTabSessionMetadata(tab);
    }
    ++it;
  }
  ReconcileWorkspaces();
  ScheduleTabTreePersistence();
  PublishCommandItems();
  runtime_presentation_changed_callbacks_.Notify();
  NotifyTabTreeSnapshotChanged();
  return tab_tree::TabTreeStore::Result::kOk;
}

bool SessionBridge::OpenNormalTabFromRemoteCommand(
    const GURL& url,
    std::optional<base::Uuid> workspace_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tab_tree_ready_ || shutting_down_ || windows_.empty() ||
      !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty() ||
      url.has_username() || url.has_password() ||
      (workspace_id && !WorkspaceExists(*workspace_id))) {
    return false;
  }
  BrowserWindowInterface* browser = windows_.begin()->first;
  if (workspace_id && !SetActiveWorkspaceForWindow(
                          browser, *workspace_id,
                          WorkspaceActivationSource::kDataReconciliation)) {
    return false;
  }
  browser->OpenGURL(url, WindowOpenDisposition::NEW_FOREGROUND_TAB);
  if (browser->GetWindow()) {
    browser->GetWindow()->Activate();
  }
  return true;
}

namespace {

std::string RuntimeStableId(std::string_view local_stable_key) {
  const size_t separator = local_stable_key.rfind(':');
  if (separator == std::string_view::npos ||
      separator + 1 == local_stable_key.size()) {
    return {};
  }
  return base::StrCat({"runtime:", local_stable_key.substr(separator + 1)});
}

}  // namespace

bool SessionBridge::FocusNormalTabFromRemoteCommand(
    std::string_view local_stable_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string stable_id = RuntimeStableId(local_stable_key);
  tabs::TabInterface* tab = FindTabForOpenTabStableId(stable_id);
  TabStripModel* model = tab ? FindTabStripModelForTab(tab) : nullptr;
  const int index =
      model && tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  model->ActivateTabAt(index);
  auto browser = model_windows_.find(model);
  if (browser != model_windows_.end() && browser->second->GetWindow()) {
    browser->second->GetWindow()->Activate();
  }
  return true;
}

bool SessionBridge::CloseNormalTabFromRemoteCommand(
    std::string_view local_stable_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string stable_id = RuntimeStableId(local_stable_key);
  tabs::TabInterface* tab = FindTabForOpenTabStableId(stable_id);
  TabStripModel* model = tab ? FindTabStripModelForTab(tab) : nullptr;
  const int index =
      model && tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  model->CloseWebContentsAt(index,
                            TabCloseTypes::CLOSE_USER_GESTURE |
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  return true;
}

}  // namespace ahoi
