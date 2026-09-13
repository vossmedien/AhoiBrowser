// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_structure_controller.h"

#include <algorithm>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "ahoi/browser/resource_policy/resource_policy_service_factory.h"
#include "ahoi/browser/session/native_workspace_structure.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/sync/profile_sync_service_factory.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/token.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"

namespace ahoi::session {
namespace {
sync::SharedSplitMetadata Metadata(const sync::SplitGroupRecord& r) {
  return {r.id, r.workspace_id, r.topology, r.ratios};
}
}  // namespace

WorkspaceStructureController::WorkspaceStructureController(
    SessionBridge* bridge,
    Profile* profile)
    : bridge_(bridge),
      bridge_lifetime_(bridge->GetWeakPtrForSync()),
      profile_(profile),
      sync_(sync::ProfileSyncServiceFactory::GetForProfile(profile)),
      resources_(resource_policy::ResourcePolicyServiceFactory::GetForProfile(
          profile)),
      clock_(sync_ ? sync_->local_device_id().AsLowercaseString()
                   : base::Uuid::GenerateRandomV4().AsLowercaseString()) {}

WorkspaceStructureController::~WorkspaceStructureController() {
  scope_->alive.store(false);
  weak_factory_.InvalidateWeakPtrs();
  if (observing_sync_) {
    sync_->RemoveObserver(this);
  }
}

void WorkspaceStructureController::Initialize() {
  if (observing_sync_ || !sync_ || !resources_) {
    return;
  }
  const auto raw = bridge_->tab_tree_store()->ReadWorkspaceStructureState();
  const auto decoded = raw ? DecodeWorkspaceStructureState(*raw) : std::nullopt;
  if (!decoded) {
    return;
  }
  state_ = *decoded;
  for (const auto& [id, entry] : state_.entries) {
    // A crash receipt is retained, but a previous process's publication lease
    // is not. Reconciliation may read it; it cannot replay a stale user action.
    if (!entry.pending.empty())
      blocked_publications_.insert(id);
  }
  clock_.Restore(state_.clock);
  sync_->AddObserver(this);
  observing_sync_ = true;
  resource_subscription_ =
      resources_->AddStatusChangedCallback(base::BindRepeating(
          [](base::WeakPtr<WorkspaceStructureController> owner,
             tabs::TabInterface*, const resource_policy::TabResourceStatus&) {
            if (owner)
              owner->OnNativeChanged();
          },
          weak_factory_.GetWeakPtr()));
  restored_subscription_ =
      SessionRestore::RegisterOnSessionRestoredCallback(base::BindRepeating(
          [](base::WeakPtr<WorkspaceStructureController> owner,
             Profile* profile, int) {
            if (owner && owner->profile_ == profile)
              owner->Schedule();
          },
          weak_factory_.GetWeakPtr()));
  Schedule();
}

sync::SyncAuthorization WorkspaceStructureController::LocalAuthority() const {
  return base::BindRepeating(
      [](std::shared_ptr<Scope> state, uint64_t epoch) {
        return state->alive.load() && state->epoch.load() == epoch;
      },
      scope_, scope_->epoch.load());
}

std::vector<base::WeakPtr<BrowserWindowInterface>>
WorkspaceStructureController::Windows() const {
  std::vector<base::WeakPtr<BrowserWindowInterface>> result;
  if (auto* collection = ProfileBrowserCollection::GetForProfile(profile_)) {
    collection->ForEach(
        [&result](BrowserWindowInterface* browser) {
          if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              !browser->IsDeleteScheduled())
            result.push_back(browser->GetWeakPtr());
          return true;
        },
        BrowserCollection::Order::kCreation, false);
  }
  return result;
}

void WorkspaceStructureController::OnNativeChanged() {
  if (scope_->applying) {
    return;
  }
  scope_->epoch.fetch_add(1);
  Schedule();
}

void WorkspaceStructureController::OnSplitChanged(
    const SplitTabChange& change) {
  if (scope_->applying || browser_shutdown::IsTryingToQuit() ||
      SessionRestore::IsRestoring(profile_) || change.model->closing_all()) {
    return;
  }
  changed_native_splits_.insert(change.split_id.ToString());
  if (change.type == SplitTabChange::Type::kRemoved &&
      change.GetRemovedChange()->reason() ==
          SplitTabChange::SplitTabRemoveReason::kSplitTabRemoved) {
    for (auto& [id, entry] : state_.entries) {
      if (entry.native_split_token != change.split_id.ToString()) {
        continue;
      }
      if (auto* split = std::get_if<sync::SplitGroupRecord>(&entry.record);
          split && !split->tombstone) {
        const auto old = entry.record;
        split->tombstone = true;
        if (!Stamp(&entry.record, &old)) {
          entry.record = old;
          continue;
        }
        local_changes_.insert(id);
        remote_authorities_.erase(id);
        entry.pending.clear();
        entry.pending_expected.clear();
        dirty_ = true;
      }
    }
  }
  OnNativeChanged();
}

void WorkspaceStructureController::Schedule() {
  if (scheduled_) {
    return;
  }
  scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceStructureController::Refresh,
                                weak_factory_.GetWeakPtr()));
}

bool WorkspaceStructureController::Stamp(sync::SyncRecord* record,
                                         const sync::SyncRecord* previous) {
  const auto stamp = clock_.Tick();
  std::visit([&](auto& r) { r.version = {.stamp = stamp}; }, *record);
  if (!sync::StampLocalMutation(previous, record)) {
    return false;
  }
  std::string canonical;
  if (!sync::SerializeRecord(*record, &canonical))
    return false;
  dirty_ = true;
  return true;
}

void WorkspaceStructureController::Refresh() {
  scheduled_ = false;
  if (!observing_sync_ || !bridge_lifetime_ || !bridge_->is_ready() ||
      persisting_ || SessionRestore::IsRestoring(profile_)) {
    return;
  }
  if (!initialized_) {
    MaterializeSplits(LocalAuthority());
    if (!bridge_lifetime_) {
      return;
    }
    initialized_ = true;
  }
  CaptureSplits();
  if (dirty_) {
    Persist(LocalAuthority(),
            base::BindOnce(
                [](base::WeakPtr<WorkspaceStructureController> owner, bool ok) {
                  if (!owner || !ok) {
                    return;
                  }
                  owner->MaterializeSplits(owner->LocalAuthority());
                  owner->ReconcileArchives(owner->LocalAuthority());
                  owner->ScanArchiveDeadline();
                  owner->ReadRemote();
                },
                weak_factory_.GetWeakPtr()));
    return;
  }
  MaterializeSplits(LocalAuthority());
  if (!bridge_lifetime_) {
    return;
  }
  ReconcileArchives(LocalAuthority());
  ScanArchiveDeadline();
  ReadRemote();
}

void WorkspaceStructureController::CaptureSplits() {
  for (const auto& browser : Windows()) {
    if (!browser) {
      continue;
    }
    auto* model = browser->GetTabStripModel();
    for (const auto native_id : model->ListSplits()) {
      auto found = std::ranges::find_if(state_.entries, [&](const auto& item) {
        return item.second.native_split_token == native_id.ToString() &&
               std::holds_alternative<sync::SplitGroupRecord>(
                   item.second.record);
      });
      if (found == state_.entries.end()) {
        // Chromium may allocate a new runtime token on SessionRestore. Match
        // the complete stable member set once, never individual pane URLs.
        const auto captured =
            CaptureNativeSplit(*bridge_, *model, native_id,
                               base::Uuid::GenerateRandomV4(), nullptr);
        if (!captured)
          continue;
        auto match = state_.entries.end();
        bool ambiguous = false;
        for (auto it = state_.entries.begin(); it != state_.entries.end();
             ++it) {
          const auto* split =
              std::get_if<sync::SplitGroupRecord>(&it->second.record);
          if (!split || split->tombstone ||
              split->workspace_id != captured->workspace_id ||
              split->topology.member_ids != captured->topology.member_ids)
            continue;
          if (match != state_.entries.end()) {
            ambiguous = true;
            break;
          }
          match = it;
        }
        if (ambiguous)
          continue;
        if (match != state_.entries.end()) {
          found = match;
          found->second.native_split_token = native_id.ToString();
          dirty_ = true;
        }
      }
      const auto id = found == state_.entries.end()
                          ? base::Uuid::GenerateRandomV4()
                          : found->first;
      const auto previous =
          found == state_.entries.end()
              ? std::optional<sync::SharedSplitMetadata>()
              : std::make_optional(Metadata(
                    std::get<sync::SplitGroupRecord>(found->second.record)));
      const auto current = CaptureNativeSplit(*bridge_, *model, native_id, id,
                                              previous ? &*previous : nullptr);
      if (!current) {
        continue;
      }
      if (found == state_.entries.end()) {
        WorkspaceStructureEntry e;
        e.record = sync::SplitGroupRecord{.id = id,
                                          .workspace_id = current->workspace_id,
                                          .topology = current->topology,
                                          .ratios = current->ratios};
        if (!Stamp(&e.record, nullptr))
          continue;
        local_changes_.insert(id);
        e.native_split_token = native_id.ToString();
        e.observed_split = current;
        state_.entries.emplace(id, std::move(e));
      } else {
        auto& e = found->second;
        auto& record = std::get<sync::SplitGroupRecord>(e.record);
        if (record.tombstone || !e.observed_split ||
            *e.observed_split == *current ||
            !changed_native_splits_.contains(native_id.ToString())) {
          continue;
        }
        const auto old = e.record;
        if (current->workspace_id != e.observed_split->workspace_id)
          record.workspace_id = current->workspace_id;
        if (current->topology != e.observed_split->topology)
          record.topology = current->topology;
        if (current->ratios != e.observed_split->ratios)
          record.ratios = current->ratios;
        if (!Stamp(&e.record, &old)) {
          e.record = old;
          continue;
        }
        local_changes_.insert(id);
        remote_authorities_.erase(id);
        e.observed_split = current;
        e.pending.clear();
        e.pending_expected.clear();
        blocked_publications_.erase(id);
        changed_native_splits_.erase(native_id.ToString());
      }
    }
  }
}

void WorkspaceStructureController::MaterializeSplits(
    sync::SyncAuthorization authority) {
  const auto lifetime = weak_factory_.GetWeakPtr();
  const auto scope = scope_;
  base::AutoReset<int> applying(&scope->applying, scope->applying + 1);
  for (auto& [id, entry] : state_.entries) {
    if (!authority.Run() || !bridge_lifetime_) {
      return;
    }
    const auto* record = std::get_if<sync::SplitGroupRecord>(&entry.record);
    if (!record) {
      continue;
    }
    auto original = authority;
    const bool previously_materialized =
        !record->tombstone && entry.observed_split == Metadata(*record);
    if (!local_changes_.contains(id) && !entry.baseline.empty() &&
        !previously_materialized) {
      const auto remote = remote_authorities_.find(id);
      if (remote == remote_authorities_.end() || !remote->second.Run())
        continue;
      original = remote->second;
    }
    std::optional<split_tabs::SplitTabId> native;
    if (const auto token = base::Token::FromString(entry.native_split_token)) {
      native = split_tabs::SplitTabId::FromRawToken(*token);
    }
    if (record->tombstone) {
      // A remote dissolve is deferred while any pane is in use/protected.
      bool protected_pane = false;
      for (const auto& member : record->topology.member_ids) {
        auto* tab = bridge_->FindTabByTreeNodeId(member);
        protected_pane |= tab && !resources_->CanArchiveTab(tab);
      }
      if (protected_pane || !original.Run())
        continue;
      for (auto browser : Windows()) {
        if (native && browser &&
            browser->GetTabStripModel()->ContainsSplit(*native)) {
          browser->GetTabStripModel()->RemoveSplit(*native);
          if (!lifetime || !bridge_lifetime_ || !original.Run())
            return;
        }
      }
      continue;
    }
    bool archived = false;
    for (const auto& member : record->topology.member_ids) {
      archived |= bridge_->tab_tree_store()->IsNodeArchived(member);
    }
    if (archived) {
      continue;
    }
    auto applied = split_tabs::SplitTabId::CreateEmpty();
    const bool materialized = MaterializeNativeSplit(
        *bridge_, Metadata(*record), native, &applied, original);
    if (!lifetime)
      return;
    if (!materialized && applied != split_tabs::SplitTabId::CreateEmpty() &&
        entry.native_split_token != applied.ToString()) {
      // Partial upstream notifications are never recaptured as fresh user
      // intent. Keep the logical binding and retry its unchanged metadata.
      entry.native_split_token = applied.ToString();
      dirty_ = true;
    }
    if (materialized) {
      const auto observed = Metadata(*record);
      if (entry.native_split_token != applied.ToString() ||
          entry.observed_split != observed) {
        entry.native_split_token = applied.ToString();
        entry.observed_split = observed;
        dirty_ = true;
      }
    }
    if (!bridge_lifetime_)
      return;
  }
  if (dirty_)
    Schedule();
}

void WorkspaceStructureController::Persist(
    sync::SyncAuthorization authority,
    base::OnceCallback<void(bool)> done,
    std::optional<tab_tree::TabTreeSnapshot> tree) {
  if (persisting_ || !bridge_lifetime_) {
    std::move(done).Run(false);
    return;
  }
  state_.clock = clock_.last();
  const auto encoded = EncodeWorkspaceStructureState(state_);
  if (!encoded) {
    std::move(done).Run(false);
    return;
  }
  persisting_ = true;
  bridge_->CommitWorkspaceStructureState(
      *encoded, std::move(authority),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceStructureController> owner,
             std::string encoded, base::OnceCallback<void(bool)> done,
             bool ok) {
            if (!owner) {
              std::move(done).Run(false);
              return;
            }
            owner->persisting_ = false;
            if (ok && EncodeWorkspaceStructureState(owner->state_) == encoded)
              owner->dirty_ = false;
            std::move(done).Run(ok);
          },
          weak_factory_.GetWeakPtr(), *encoded, std::move(done)),
      std::move(tree));
}

void WorkspaceStructureController::OnAhoiDeviceTabsChanged(
    const sync::DeviceTabsSnapshot&) {
  Schedule();
}
void WorkspaceStructureController::OnAhoiSyncStatusChanged(
    const sync::SyncTransportStatus&) {
  Schedule();
}

}  // namespace ahoi::session
