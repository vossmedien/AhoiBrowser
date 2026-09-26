// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AHOI_BROWSER_SESSION_WORKSPACE_STRUCTURE_CONTROLLER_H_
#define AHOI_BROWSER_SESSION_WORKSPACE_STRUCTURE_CONTROLLER_H_

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "ahoi/browser/session/group_page_close.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class Profile;
class BrowserWindowInterface;
namespace ahoi {
class SessionBridge;
}
namespace ahoi::resource_policy {
class ResourcePolicyService;
}

namespace ahoi::session {

// What CaptureSplits adopts from an observed native split into its stored
// split record. Exposed for unit tests (handoff 011 S6).
enum class SplitCaptureAdoption {
  kNone,
  // A native split change (topology, ratios, membership) was observed.
  kNativeChange,
  // Only the Workspace of the members changed, e.g. after moving the split's
  // pages to another Workspace in the tree. No native split change happens,
  // but the record must follow its members, or materializing it later fails
  // on the Workspace mismatch.
  kWorkspaceOnly,
};
SplitCaptureAdoption ClassifySplitCapture(
    bool native_change,
    bool record_tombstone,
    const std::optional<sync::SharedSplitMetadata>& observed,
    const sync::SharedSplitMetadata& current,
    const base::Uuid& record_workspace_id);

// Whether an observed native event renews the structure authority epoch and
// so cancels an in-flight structure commit (handoff 011 S3). Only changes of
// tab-strip structure, tree structure, split membership or archive membership
// do. Selection, title/URL/presentation metadata and a resource status that
// does not newly protect a page only reschedule a refresh.
bool TabStripChangeInvalidatesStructure(TabStripModelChange::Type type);
bool TreeChangeInvalidatesStructure(tab_tree::MutationKind kind);
bool ResourceChangeInvalidatesStructure(bool was_protected,
                                        bool now_protected);

// Handoff 058: how long a split closed by an archive is still recognized as
// "closed by the archive" (011 S2). The agreed pages normally close within
// their unload handlers; a page that is still open after this (for example a
// hung renderer) must not keep a later manual dissolve of its split from
// tombstoning the record.
inline constexpr base::TimeDelta kArchiveCloseGrace = base::Seconds(30);
bool ArchiveCloseTokenLive(base::TimeTicks marked, base::TimeTicks now);

class WorkspaceStructureController final
    : public sync::ProfileSyncService::Observer {
 public:
  WorkspaceStructureController(SessionBridge* bridge, Profile* profile);
  ~WorkspaceStructureController() override;
  void Initialize();
  void OnNativeChanged();
  // A native event that changes no structure: refresh without cancelling an
  // in-flight structure commit (handoff 011 S3).
  void OnNativeMetadataChanged();
  void OnSplitChanged(const SplitTabChange& change);
  void Archive(const std::vector<base::Uuid>& nodes,
               sync::SharedArchiveReason reason,
               base::OnceCallback<void(bool)> done);
  void Restore(base::Uuid entry_id,
               base::OnceCallback<void(bool)> done,
               std::optional<tab_tree::ArchiveRestorePlacement> placement =
                   std::nullopt);
  std::vector<sync::TabArchiveEntryRecord> Archives() const;
  // Handoff 072: after the user opened a member of a live split record (and
  // its partners), build that split now. The passive MaterializeSplits defers
  // splits with an active or loading pane, so a split restored from the
  // archive never came back while the user stayed on it.
  bool MaterializeSplitForActivation(const base::Uuid& member_node_id);
  void DeleteArchive(sync::TabArchiveEntryRecord expected,
                     base::OnceCallback<void(bool)> done);
  void OnAhoiDeviceTabsChanged(const sync::DeviceTabsSnapshot&) override;
  void OnAhoiSyncStatusChanged(const sync::SyncTransportStatus&) override;

 private:
  friend class ::ahoi::SessionBridge;
  void Schedule();
  void Refresh();
  void OnResourceStatusChanged(tabs::TabInterface* tab);
  void CaptureSplits();
  void MaterializeSplits(sync::SyncAuthorization authority);
  void ScanArchiveDeadline();
  void CloseArchived(base::Uuid entry_id, sync::SyncAuthorization authority);
  // Handoff 011 S2: remembers the native splits of an archive's pages right
  // before they close, so OnSplitChanged does not read the resulting split
  // removal as a user dissolve and tombstone the record the restore needs.
  void MarkSplitsClosingForArchive(const base::Uuid& archive_id);
  // Handoff 006: archive only after every live page of the group agreed to
  // close; the entry is written first, then exactly those pages close.
  void ArchiveAgreedPages(const std::vector<base::Uuid>& nodes,
                          sync::SharedArchiveReason reason,
                          base::OnceCallback<void(bool)> done);
  void OnArchivePagesAnswered(std::vector<base::Uuid> nodes,
                              sync::SharedArchiveReason reason,
                              base::OnceCallback<void(bool)> done,
                              bool all_agreed);
  void ReconcileArchives(sync::SyncAuthorization authority);
  bool CanArchive(const std::vector<base::Uuid>& ids) const;
  bool CanRestore(const sync::TabArchiveEntryRecord& archive) const;
  void ReadRemote();
  void OnRemote(std::optional<sync::WorkspaceStructureProjection> projection);
  void PublishNext(const sync::WorkspaceStructureProjection& projection);
  void OnPublished(base::Uuid id,
                   std::string payload,
                   sync::SyncAuthorization original,
                   bool success);
  void Persist(sync::SyncAuthorization authority,
               base::OnceCallback<void(bool)> done,
               std::optional<tab_tree::TabTreeSnapshot> tree = std::nullopt);
  sync::SyncAuthorization LocalAuthority() const;
  bool Stamp(sync::SyncRecord* record, const sync::SyncRecord* previous);
  std::vector<base::WeakPtr<BrowserWindowInterface>> Windows() const;
  raw_ptr<SessionBridge> bridge_;
  base::WeakPtr<sync::ProfileSyncUiBridge> bridge_lifetime_;
  raw_ptr<Profile> profile_;
  raw_ptr<sync::ProfileSyncService> sync_;
  raw_ptr<resource_policy::ResourcePolicyService> resources_;
  WorkspaceStructureState state_;
  sync::HybridLogicalClock clock_;
  struct Scope {
    std::atomic<bool> alive{true};
    std::atomic<uint64_t> epoch{0};
    int applying = 0;
  };
  std::shared_ptr<Scope> scope_ = std::make_shared<Scope>();
  base::CallbackListSubscription resource_subscription_;
  base::CallbackListSubscription restored_subscription_;
  base::OneShotTimer archive_timer_;
  bool initialized_ = false;
  bool observing_sync_ = false;
  bool scheduled_ = false;
  bool persisting_ = false;
  bool read_pending_ = false;
  bool publish_pending_ = false;
  bool dirty_ = false;
  // A persisted remote record is data, not renewed import authority. On a
  // restart remote effects wait for ReadWorkspaceStructure's fresh lease.
  std::map<base::Uuid, sync::SyncAuthorization> remote_authorities_;
  std::set<base::Uuid> local_changes_;
  std::set<std::string> changed_native_splits_;
  // Native split token -> when the archive started closing its pages.
  std::map<std::string, base::TimeTicks> archive_closing_splits_;
  // Tabs last seen as not archivable. Only the transition into protection
  // cancels a pending archive or remote dissolve (handoff 011 S3).
  std::set<tabs::TabHandle> protected_tabs_;
  std::set<base::Uuid> blocked_publications_;
  std::unique_ptr<GroupPageClose> archive_close_;
  base::WeakPtrFactory<WorkspaceStructureController> weak_factory_{this};
};
}  // namespace ahoi::session
#endif
