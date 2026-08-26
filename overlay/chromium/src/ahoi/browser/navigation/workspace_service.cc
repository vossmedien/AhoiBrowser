// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/workspace_service.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <utility>

namespace ahoi {

namespace {

bool IsWorkspaceValid(const tab_tree::Workspace& workspace) {
  return workspace.model_version == tab_tree::kCurrentModelVersion &&
         workspace.id.is_valid() && !workspace.name.empty() &&
         !workspace.sort_key.empty() && !workspace.created_at.is_null() &&
         !workspace.modified_at.is_null();
}

}  // namespace

WorkspaceService::WorkspaceService() = default;
WorkspaceService::~WorkspaceService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void WorkspaceService::AddObserver(WorkspaceServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void WorkspaceService::RemoveObserver(WorkspaceServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool WorkspaceService::ReplaceWorkspaces(
    std::vector<tab_tree::Workspace> workspaces) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::set<base::Uuid> input_ids;
  for (const auto& workspace : workspaces) {
    if (!IsWorkspaceValid(workspace) ||
        !input_ids.insert(workspace.id).second) {
      return false;
    }
  }
  std::erase_if(workspaces, [](const tab_tree::Workspace& workspace) {
    return workspace.tombstone;
  });

  std::set<base::Uuid> visible_ids;
  for (const auto& workspace : workspaces) {
    visible_ids.insert(workspace.id);
  }

  std::sort(workspaces.begin(), workspaces.end(),
            [](const tab_tree::Workspace& lhs, const tab_tree::Workspace& rhs) {
              if (lhs.sort_key != rhs.sort_key) {
                return lhs.sort_key < rhs.sort_key;
              }
              return lhs.id < rhs.id;
            });

  if (ordered_workspaces_ == workspaces) {
    return true;
  }

  struct ClearedActivation {
    base::Uuid window_id;
    base::Uuid workspace_id;
  };
  std::vector<ClearedActivation> cleared;
  for (auto it = active_workspaces_.begin(); it != active_workspaces_.end();) {
    if (!visible_ids.contains(it->second)) {
      cleared.push_back({it->first, it->second});
      it = active_workspaces_.erase(it);
    } else {
      ++it;
    }
  }

  ordered_workspaces_ = std::move(workspaces);
  for (auto& observer : observers_) {
    observer.OnWorkspaceListChanged();
  }
  for (const auto& activation : cleared) {
    for (auto& observer : observers_) {
      observer.OnActiveWorkspaceChanged(
          activation.window_id, activation.workspace_id, std::nullopt,
          WorkspaceActivationSource::kDataReconciliation);
    }
  }
  return true;
}

bool WorkspaceService::SetActiveWorkspace(const base::Uuid& window_id,
                                          const base::Uuid& workspace_id,
                                          WorkspaceActivationSource source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!window_id.is_valid() || !FindWorkspaceIndex(workspace_id).has_value()) {
    return false;
  }

  std::optional<base::Uuid> old_workspace_id;
  auto active_it = active_workspaces_.find(window_id);
  if (active_it != active_workspaces_.end()) {
    old_workspace_id = active_it->second;
    if (active_it->second == workspace_id) {
      return true;
    }
    active_it->second = workspace_id;
  } else {
    active_workspaces_.emplace(window_id, workspace_id);
  }

  for (auto& observer : observers_) {
    observer.OnActiveWorkspaceChanged(window_id, old_workspace_id, workspace_id,
                                      source);
  }
  return true;
}

std::optional<base::Uuid> WorkspaceService::GetActiveWorkspace(
    const base::Uuid& window_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = active_workspaces_.find(window_id);
  return it == active_workspaces_.end() ? std::nullopt
                                        : std::make_optional(it->second);
}

std::optional<base::Uuid> WorkspaceService::ActivateRelative(
    const base::Uuid& window_id,
    int delta,
    bool wrap,
    WorkspaceActivationSource source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!window_id.is_valid() || delta == 0 || ordered_workspaces_.empty()) {
    return std::nullopt;
  }

  const int64_t count = static_cast<int64_t>(ordered_workspaces_.size());
  int64_t next_index;
  const std::optional<base::Uuid> active = GetActiveWorkspace(window_id);
  if (!active.has_value()) {
    next_index = delta > 0 ? static_cast<int64_t>(delta) - 1
                           : count + static_cast<int64_t>(delta);
  } else {
    const std::optional<size_t> current = FindWorkspaceIndex(*active);
    if (!current.has_value()) {
      return std::nullopt;
    }
    next_index = static_cast<int64_t>(*current) + static_cast<int64_t>(delta);
  }

  if (wrap) {
    next_index = ((next_index % count) + count) % count;
  } else if (next_index < 0 || next_index >= count) {
    return std::nullopt;
  }

  const base::Uuid selected =
      ordered_workspaces_[static_cast<size_t>(next_index)].id;
  if (!SetActiveWorkspace(window_id, selected, source)) {
    return std::nullopt;
  }
  return selected;
}

void WorkspaceService::ForgetWindow(const base::Uuid& window_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_workspaces_.erase(window_id);
}

std::optional<size_t> WorkspaceService::FindWorkspaceIndex(
    const base::Uuid& workspace_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = std::ranges::find(ordered_workspaces_, workspace_id,
                              &tab_tree::Workspace::id);
  if (it == ordered_workspaces_.end()) {
    return std::nullopt;
  }
  return static_cast<size_t>(std::distance(ordered_workspaces_.begin(), it));
}

}  // namespace ahoi
