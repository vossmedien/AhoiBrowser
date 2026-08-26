// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_WORKSPACE_SERVICE_H_
#define AHOI_BROWSER_NAVIGATION_WORKSPACE_SERVICE_H_

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/uuid.h"
#include "components/keyed_service/core/keyed_service.h"

namespace ahoi {

// The UI path that requested a workspace activation. This travels with the
// notification so views can avoid replaying the initiating animation.
enum class WorkspaceActivationSource {
  kSidebar = 0,
  kKeyboard = 1,
  kGesture = 2,
  kRestore = 3,
  kDataReconciliation = 4,
};

class WorkspaceServiceObserver : public base::CheckedObserver {
 public:
  virtual void OnWorkspaceListChanged() = 0;
  virtual void OnActiveWorkspaceChanged(
      const base::Uuid& window_id,
      const std::optional<base::Uuid>& old_workspace_id,
      const std::optional<base::Uuid>& new_workspace_id,
      WorkspaceActivationSource source) = 0;

 protected:
  ~WorkspaceServiceObserver() override = default;
};

// Runtime authority intended to be owned by one regular Profile's keyed-service
// factory. It tracks ordered workspaces and each window's active workspace.
// Persistence remains in TabTreeStore; SessionBridge will feed snapshots into
// this service and persist activation changes.
class WorkspaceService : public KeyedService {
 public:
  WorkspaceService();
  WorkspaceService(const WorkspaceService&) = delete;
  WorkspaceService& operator=(const WorkspaceService&) = delete;
  ~WorkspaceService() override;

  void AddObserver(WorkspaceServiceObserver* observer);
  void RemoveObserver(WorkspaceServiceObserver* observer);

  // Atomically replaces the visible workspace snapshot. Tombstones are
  // filtered, order is derived from sort_key, and any duplicate or invalid
  // input row rejects the entire update without mutating current state.
  [[nodiscard]] bool ReplaceWorkspaces(
      std::vector<tab_tree::Workspace> workspaces);

  const std::vector<tab_tree::Workspace>& ordered_workspaces() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return ordered_workspaces_;
  }

  [[nodiscard]] bool SetActiveWorkspace(const base::Uuid& window_id,
                                        const base::Uuid& workspace_id,
                                        WorkspaceActivationSource source);

  std::optional<base::Uuid> GetActiveWorkspace(
      const base::Uuid& window_id) const;

  // Moves by `delta` in visual workspace order. If this window has no active
  // workspace yet, positive deltas start at the first item and negative deltas
  // start at the last item.
  std::optional<base::Uuid> ActivateRelative(const base::Uuid& window_id,
                                             int delta,
                                             bool wrap,
                                             WorkspaceActivationSource source);

  // Removes ephemeral per-window state. Persistent workspace data is
  // unaffected.
  void ForgetWindow(const base::Uuid& window_id);

 private:
  std::optional<size_t> FindWorkspaceIndex(
      const base::Uuid& workspace_id) const;

  std::vector<tab_tree::Workspace> ordered_workspaces_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<base::Uuid, base::Uuid> active_workspaces_
      GUARDED_BY_CONTEXT(sequence_checker_);
  base::ObserverList<WorkspaceServiceObserver> observers_
      GUARDED_BY_CONTEXT(sequence_checker_);
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_NAVIGATION_WORKSPACE_SERVICE_H_
