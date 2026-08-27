// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_BRIDGE_H_
#define AHOI_BROWSER_SESSION_SESSION_BRIDGE_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/navigation/workspace_service.h"
#include "ahoi/browser/session/session_restore_integration.h"
#include "ahoi/browser/sync/profile_sync_ui_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "ahoi/browser/tab_tree/tab_tree_observer.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;
class TabStripModel;

namespace content {
class WebContents;
}

namespace ahoi {

class CommandService;

inline constexpr char kTabTreeDatabaseFilename[] = "Ahoi Tab Tree";

// Profile-scoped runtime join between Ahoi's persistent UUID model and
// Chromium's native window/tab/content objects. TabInterface is the canonical
// runtime identity: it survives a tab drag between normal windows and a
// WebContents replacement caused by discarding.
//
// The bridge observes only TYPE_NORMAL windows belonging to its exact regular
// Profile. OTR profiles are not redirected here. The owned TabTreeStore is the
// persistence authority and opens one SQLite database inside the profile.
class SessionBridge : public KeyedService,
                      public BrowserCollectionObserver,
                      public TabStripModelObserver,
                      public WorkspaceServiceObserver,
                      public tab_tree::TabTreeObserver,
                      public session::WorkspaceSessionMetadataProvider,
                      public sync::ProfileSyncUiBridge {
 public:
  SessionBridge(Profile* profile,
                WorkspaceService* workspace_service,
                CommandService* command_service);
  SessionBridge(const SessionBridge&) = delete;
  SessionBridge& operator=(const SessionBridge&) = delete;
  ~SessionBridge() override;

  // KeyedService:
  void Shutdown() override;

  bool is_operational() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return !shutting_down_;
  }

  bool is_ready() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return tab_tree_ready_ && !shutting_down_;
  }

  // Deterministic synchronization seams for the focused bridge tests. Product
  // UI never waits for disk: it renders the in-memory store immediately and
  // is refreshed when the background load completes.
  void RunWhenReadyForTesting(base::OnceClosure callback);
  void FlushPersistenceForTesting(base::OnceClosure callback);
  // Serializes the current authoritative tree through the persistence task
  // runner and reports whether the durable snapshot was written. Importers use
  // this before copying the SQLite database into a rollback backup.
  void FlushPersistenceForBackup(base::OnceCallback<void(bool)> callback);
  base::WeakPtr<sync::ProfileSyncUiBridge> GetWeakPtrForSync() override;
  base::CallbackListSubscription AddRuntimePresentationChangedCallback(
      base::RepeatingClosure callback);
  base::CallbackListSubscription AddTabTreeSnapshotChangedCallback(
      base::RepeatingCallback<void(const tab_tree::TabTreeSnapshot&)> callback)
      override;
  void RequestLocalTabCapture() override;

  [[nodiscard]] bool ExportTabTreeSnapshot(
      tab_tree::TabTreeSnapshot* snapshot) override;
  // Applies a merged/repaired provider snapshot through the regular tree
  // authority, retaining local undo operations and notifying every runtime/UI
  // observer. It never writes a parallel sync-owned tree.
  [[nodiscard]] tab_tree::TabTreeStore::Result ApplySyncedTabTreeSnapshot(
      tab_tree::TabTreeSnapshot snapshot) override;

  // Narrow UI-thread execution seam for an already authenticated, single-tab
  // remote command. Callers still own policy/signature/replay validation.
  [[nodiscard]] bool OpenNormalTabFromRemoteCommand(
      const GURL& url,
      std::optional<base::Uuid> workspace_id) override;
  [[nodiscard]] bool FocusNormalTabFromRemoteCommand(
      std::string_view local_stable_key) override;
  [[nodiscard]] bool CloseNormalTabFromRemoteCommand(
      std::string_view local_stable_key) override;

  size_t tracked_window_count() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return windows_.size();
  }
  size_t tracked_tab_count() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return runtime_tabs_.size();
  }

  tab_tree::TabTreeStore* tab_tree_store() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return tab_tree_store_.get();
  }

  std::optional<base::Uuid> GetWindowId(
      const BrowserWindowInterface* browser) const;
  BrowserWindowInterface* FindWindowById(const base::Uuid& window_id) const;

  bool SetActiveWorkspaceForWindow(BrowserWindowInterface* browser,
                                   const base::Uuid& workspace_id,
                                   WorkspaceActivationSource source);
  std::optional<base::Uuid> ActivateRelativeWorkspaceForWindow(
      BrowserWindowInterface* browser,
      int delta,
      WorkspaceActivationSource source);
  std::optional<base::Uuid> GetActiveWorkspaceForWindow(
      const BrowserWindowInterface* browser) const;

  // SessionService integration. Snapshot getters return only validated Ahoi
  // state. Restore accepts stale workspace/node identities fail-safe: a
  // missing workspace falls back to the first persisted workspace, while a
  // missing or conflicting saved-page node leaves the tab temporary.
  std::optional<session::WindowSessionMetadata> GetWindowSessionMetadata(
      const BrowserWindowInterface* browser) const override;
  std::optional<session::TabSessionMetadata> GetTabSessionMetadata(
      const tabs::TabInterface* tab) const override;
  [[nodiscard]] bool RestoreWindowSessionMetadata(
      BrowserWindowInterface* browser,
      const session::WindowSessionMetadata& metadata) override;
  [[nodiscard]] bool RestoreTabSessionMetadata(
      tabs::TabInterface* tab,
      const session::TabSessionMetadata& metadata) override;
  std::optional<base::Uuid> CreateWorkspace(
      std::u16string name,
      std::u16string icon,
      std::optional<uint32_t> accent_argb);
  // Duplicates one active workspace, including its complete saved-page tree.
  // The duplicate receives a fresh identity and is placed directly after the
  // source in workspace order. No ID is returned unless the store mutation
  // and the in-memory workspace snapshot both succeed.
  std::optional<base::Uuid> DuplicateWorkspace(
      const base::Uuid& source_workspace_id,
      std::u16string name,
      std::u16string icon,
      std::optional<uint32_t> accent_argb);
  [[nodiscard]] tab_tree::TabTreeStore::Result UpdateWorkspacePresentation(
      const base::Uuid& workspace_id,
      std::u16string name,
      std::u16string icon,
      std::optional<uint32_t> accent_argb);
  [[nodiscard]] tab_tree::TabTreeStore::Result DeleteWorkspace(
      const base::Uuid& workspace_id);

  // Binds one validated, active saved-page row to a currently tracked native
  // tab. A persistent node cannot be bound to two runtime tabs. Rebinding one
  // tab to another node is atomic from callers' perspective.
  [[nodiscard]] bool BindTreeNodeToTab(const tab_tree::TreeNode& node,
                                       tabs::TabInterface* tab);
  // Promotes a live temporary tab into a persistent saved-page row at the end
  // of the active workspace root. Repeated calls are idempotent and return the
  // existing durable node id. The bridge is never constructed for OTR
  // profiles, so this path cannot persist incognito state.
  std::optional<base::Uuid> SaveTabAtWorkspaceRoot(
      BrowserWindowInterface* browser,
      tabs::TabInterface* tab);
  void UnbindTreeNodeFromTab(tabs::TabInterface* tab);
  // Converts a saved runtime tab back to a temporary tab while retaining its
  // workspace assignment, so it appears below the saved-tree separator.
  void MakeTabTemporary(tabs::TabInterface* tab);

  tabs::TabInterface* FindTabByTreeNodeId(const base::Uuid& node_id) const;
  // Resolves the stable id published for a command-bar open-tab item. Saved
  // tabs use their durable tree UUID; temporary tabs use a process-local
  // TabHandle id prefixed with "runtime:".
  tabs::TabInterface* FindTabForOpenTabStableId(
      std::string_view stable_id) const;
  std::optional<base::Uuid> FindTreeNodeIdForTab(
      const tabs::TabInterface* tab) const;
  std::optional<base::Uuid> GetWorkspaceForTab(
      const tabs::TabInterface* tab) const;
  // Returns the runtime tab restored or most recently selected for one
  // workspace in `browser`. The weak entry is owned by the bridge and is
  // discarded as soon as the tab leaves the tracked window/workspace.
  tabs::TabInterface* GetLastActiveTabForWorkspace(
      const BrowserWindowInterface* browser,
      const base::Uuid& workspace_id) const;
  tabs::TabInterface* FindTabByWebContents(
      const content::WebContents* contents) const;
  TabStripModel* FindTabStripModelForTab(const tabs::TabInterface* tab) const;
  content::WebContents* FindWebContentsForTab(
      const tabs::TabInterface* tab) const;

 private:
  enum class TabTreeLoadStatus {
    kMissing,
    kLoaded,
    kFailed,
  };

  struct TabTreeLoadResult {
    TabTreeLoadStatus status = TabTreeLoadStatus::kFailed;
    tab_tree::TabTreeSnapshot snapshot;
  };

  struct WindowState {
    base::Uuid window_id;
    raw_ptr<TabStripModel> tab_strip_model = nullptr;
    std::map<base::Uuid, base::WeakPtr<tabs::TabInterface>> last_active_tabs;
  };

  struct RuntimeTabState {
    raw_ptr<TabStripModel> tab_strip_model = nullptr;
    base::WeakPtr<tabs::TabInterface> tab;
    base::WeakPtr<content::WebContents> web_contents;
    std::optional<base::Uuid> node_id;
    std::optional<base::Uuid> workspace_id;
    // Distinguishes an intentionally restored temporary tab (no node id) from
    // a newly opened tab that may still be matched to a saved page by URL.
    bool restored_session_metadata_applied = false;
    std::u16string last_observed_title;
    GURL last_observed_url;
    base::CallbackListSubscription tab_ui_change_subscription;
    base::CallbackListSubscription will_discard_contents_subscription;
    base::CallbackListSubscription will_detach_subscription;
    base::CallbackListSubscription did_insert_subscription;
  };

  bool ShouldTrackBrowser(const BrowserWindowInterface* browser) const;
  void TrackBrowser(BrowserWindowInterface* browser);
  void UntrackBrowser(BrowserWindowInterface* browser,
                      bool tab_strip_model_destroyed);
  void TrackRuntimeTab(TabStripModel* model,
                       tabs::TabInterface* tab,
                       content::WebContents* contents);
  void RemoveRuntimeTab(tabs::TabInterface* tab);
  void UpdateRuntimeTabContents(tabs::TabInterface* tab,
                                content::WebContents* old_contents,
                                content::WebContents* new_contents);
  [[nodiscard]] bool InitializeTabTree();
  void BeginTabTreeLoad();
  void OnTabTreeLoaded(TabTreeLoadResult result);
  [[nodiscard]] bool FinishRuntimeInitialization();
  void ScheduleTabTreePersistence();
  void PersistTabTreeNow();
  void NotifyTabTreeSnapshotChanged();
  static TabTreeLoadResult LoadTabTreeSnapshot(const base::FilePath& path);
  static bool PersistTabTreeSnapshot(const base::FilePath& path,
                                     tab_tree::TabTreeSnapshot snapshot);
  void EnsureTreeNodeForTab(tabs::TabInterface* tab);
  void ScheduleTreeNodeBinding(tabs::TabInterface* tab);
  void UnbindTreeNodeFromTabInternal(tabs::TabInterface* tab,
                                     bool clear_workspace);
  void PublishCommandItems();

  // Workspace session continuity lives in a separate implementation unit so
  // Chromium session seams do not leak into the persistent tree/runtime code.
  std::vector<base::Uuid> OrderedWorkspaceIdsForSession() const;
  void ApplyPendingSessionMetadata();
  [[nodiscard]] bool ApplyWindowSessionMetadataNow(
      BrowserWindowInterface* browser,
      const session::WindowSessionMetadata& metadata);
  [[nodiscard]] bool ApplyTabSessionMetadataNow(
      tabs::TabInterface* tab,
      const session::TabSessionMetadata& metadata);
  void PersistWindowSessionMetadata(BrowserWindowInterface* browser);
  void PersistTabSessionMetadata(tabs::TabInterface* tab);
  void UpdateLastActiveTab(TabStripModel* model, tabs::TabInterface* tab);
  void RestoreLastActiveTabFlag(tabs::TabInterface* tab, bool last_active);
  void RemoveTabFromLastActiveState(tabs::TabInterface* tab);

  void OnTabWillDiscardContents(tabs::TabInterface* tab,
                                content::WebContents* old_contents,
                                content::WebContents* new_contents);
  void OnTabWillDetach(tabs::TabInterface* tab,
                       tabs::TabInterface::DetachReason reason);
  void OnTabDidInsert(tabs::TabInterface* tab);
  void OnTabUIChanged(tabs::TabInterface* tab);
  void RemoveDetachedTabIfStillUnattached(
      tabs::TabInterface* tab,
      base::WeakPtr<tabs::TabInterface> tab_weak_ptr);

  bool WorkspaceExists(const base::Uuid& workspace_id) const;
  bool RefreshWorkspaceSnapshot();
  void ScheduleWorkspaceReconciliation();
  void ReconcileWorkspaces();

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // WorkspaceServiceObserver:
  void OnWorkspaceListChanged() override;
  void OnActiveWorkspaceChanged(
      const base::Uuid& window_id,
      const std::optional<base::Uuid>& old_workspace_id,
      const std::optional<base::Uuid>& new_workspace_id,
      WorkspaceActivationSource source) override;

  // tab_tree::TabTreeObserver:
  void OnTabTreeChanged(const tab_tree::TabTreeChange& change) override;

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  raw_ptr<CommandService> command_service_ = nullptr;
  std::unique_ptr<tab_tree::TabTreeStore> tab_tree_store_;
  base::FilePath tab_tree_database_path_;
  scoped_refptr<base::SequencedTaskRunner> persistence_task_runner_;
  base::OneShotTimer persistence_timer_;
  base::OnceClosure ready_callback_for_testing_;
  base::ScopedObservation<ProfileBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::RepeatingClosureList runtime_presentation_changed_callbacks_;
  base::RepeatingCallbackList<void(const tab_tree::TabTreeSnapshot&)>
      tab_tree_snapshot_changed_callbacks_;

  std::map<BrowserWindowInterface*, WindowState> windows_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<base::Uuid, raw_ptr<BrowserWindowInterface>> id_windows_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<TabStripModel*, raw_ptr<BrowserWindowInterface>> model_windows_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<tabs::TabInterface*, RuntimeTabState> runtime_tabs_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<base::Uuid, base::WeakPtr<tabs::TabInterface>> node_tabs_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<content::WebContents*, base::WeakPtr<tabs::TabInterface>>
      contents_tabs_ GUARDED_BY_CONTEXT(sequence_checker_);

  struct PendingWindowSessionMetadata {
    base::WeakPtr<BrowserWindowInterface> browser;
    session::WindowSessionMetadata metadata;
  };
  struct PendingTabSessionMetadata {
    base::WeakPtr<tabs::TabInterface> tab;
    session::TabSessionMetadata metadata;
  };
  std::vector<PendingWindowSessionMetadata> pending_window_session_metadata_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::vector<PendingTabSessionMetadata> pending_tab_session_metadata_
      GUARDED_BY_CONTEXT(sequence_checker_);

  bool workspace_reconciliation_scheduled_
      GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool tab_tree_ready_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool persistence_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = true;
  bool session_metadata_provider_registered_
      GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool shutting_down_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<SessionBridge> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_SESSION_SESSION_BRIDGE_H_
