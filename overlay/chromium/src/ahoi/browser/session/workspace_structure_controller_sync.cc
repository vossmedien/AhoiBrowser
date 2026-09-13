// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_structure_controller.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/functional/bind.h"

namespace ahoi::session {
void WorkspaceStructureController::ReadRemote() {
  if (read_pending_ || publish_pending_ || persisting_ ||
      !sync_->sync_enabled()) {
    return;
  }
  read_pending_ = true;
  sync_->ReadWorkspaceStructure(base::BindOnce(
      &WorkspaceStructureController::OnRemote, weak_factory_.GetWeakPtr()));
}

void WorkspaceStructureController::OnRemote(
    std::optional<sync::WorkspaceStructureProjection> projection) {
  read_pending_ = false;
  if (!projection || !projection->initial_fetch_complete ||
      !projection->authorization.Run() || !bridge_lifetime_ || persisting_) {
    return;
  }
  // Keep this projection bound to the local revision observed at consumption,
  // including the asynchronous persistence and native materialization steps.
  projection->authorization = base::BindRepeating(
      [](sync::SyncAuthorization provider, sync::SyncAuthorization local) {
        return provider.Run() && local.Run();
      },
      projection->authorization, LocalAuthority());
  if (clock_.last() < projection->observed_clock) {
    clock_.Observe(projection->observed_clock);
  }
  const auto before = state_;
  auto consume = [&](const sync::SyncRecord& incoming) {
    const auto id = sync::GetEntityId(incoming);
    const auto payload = projection->canonical_payloads.find(id);
    if (payload == projection->canonical_payloads.end()) {
      return;
    }
    remote_authorities_.insert_or_assign(id, projection->authorization);
    auto found = state_.entries.find(id);
    if (found == state_.entries.end()) {
      WorkspaceStructureEntry entry;
      entry.record = incoming;
      entry.baseline = payload->second;
      state_.entries.emplace(id, std::move(entry));
      dirty_ = true;
      return;
    }
    auto& entry = found->second;
    sync::SyncRecord merged;
    const auto decision =
        sync::MergeRecordFields(entry.record, incoming, &merged);
    if (decision == sync::MergeDecision::kInvalid) {
      blocked_publications_.insert(id);
      return;
    }
    if (decision == sync::MergeDecision::kAcceptIncoming) {
      merged = incoming;
    }
    if (decision == sync::MergeDecision::kKeepExisting ||
        decision == sync::MergeDecision::kDuplicate) {
      merged = entry.record;
    }
    if (entry.record != merged || entry.baseline != payload->second) {
      if (entry.record != merged)
        local_changes_.erase(id);
      entry.record = std::move(merged);
      entry.baseline = payload->second;
      if (!entry.pending.empty() && entry.pending != payload->second &&
          entry.pending_expected != payload->second) {
        blocked_publications_.insert(id);
      }
      if (entry.pending == payload->second) {
        entry.pending.clear();
        entry.pending_expected.clear();
      }
      dirty_ = true;
    }
  };
  for (const auto& record : projection->split_groups) {
    consume(record);
  }
  for (const auto& record : projection->archive_entries) {
    consume(record);
  }
  if (dirty_) {
    auto original = projection->authorization;
    const auto attempted = state_.entries;
    Persist(original,
            base::BindOnce(
                [](base::WeakPtr<WorkspaceStructureController> owner,
                   sync::WorkspaceStructureProjection p,
                   WorkspaceStructureState before,
                   std::map<base::Uuid, WorkspaceStructureEntry> attempted,
                   bool success) {
                  if (!owner)
                    return;
                  if (!success) {
                    // Do not let a failed imported state escape via an
                    // unrelated local persistence later. Preserve native edits
                    // made during the wait.
                    for (const auto& [id, entry] : attempted) {
                      auto current = owner->state_.entries.find(id);
                      if (current == owner->state_.entries.end() ||
                          current->second != entry)
                        continue;
                      auto prior = before.entries.find(id);
                      if (prior == before.entries.end())
                        owner->state_.entries.erase(current);
                      else
                        current->second = prior->second;
                    }
                    return;
                  }
                  if (!p.authorization.Run())
                    return;
                  owner->MaterializeSplits(p.authorization);
                  if (owner)
                    owner->ReconcileArchives(p.authorization);
                  if (owner)
                    owner->PublishNext(p);
                },
                weak_factory_.GetWeakPtr(), std::move(*projection), before,
                attempted));
  } else {
    MaterializeSplits(projection->authorization);
    ReconcileArchives(projection->authorization);
    PublishNext(*projection);
  }
}

void WorkspaceStructureController::PublishNext(
    const sync::WorkspaceStructureProjection& projection) {
  if (publish_pending_ || persisting_ || !projection.authorization.Run()) {
    return;
  }
  for (auto& [id, entry] : state_.entries) {
    if (blocked_publications_.contains(id)) {
      continue;
    }
    std::string local;
    if (!sync::SerializeRecord(entry.record, &local) ||
        local == entry.baseline) {
      continue;
    }
    const auto current = projection.canonical_payloads.find(id);
    const std::string expected =
        current == projection.canonical_payloads.end() ? "" : current->second;
    if (entry.pending.empty()) {
      // Field clocks remain the original local/merged clocks. Only this new
      // enclosing merge transaction gets a local clock; a retry uses its exact
      // persisted bytes and never runs this branch again.
      sync::SyncRecord outgoing = entry.record;
      const auto stamp = clock_.Tick();
      std::visit([&](auto& r) { r.version = {.stamp = stamp}; }, outgoing);
      if (!sync::HasCompleteFieldVersions(outgoing) ||
          !sync::SerializeRecord(outgoing, &entry.pending)) {
        continue;
      }
      entry.pending_expected = expected;
      dirty_ = true;
    }
    const std::string pending = entry.pending;
    const std::string pending_expected = entry.pending_expected;
    auto original = base::BindRepeating(
        [](sync::SyncAuthorization remote, sync::SyncAuthorization local) {
          return remote.Run() && local.Run();
        },
        projection.authorization, LocalAuthority());
    publish_pending_ = true;
    Persist(original,
            base::BindOnce(
                [](base::WeakPtr<WorkspaceStructureController> owner,
                   base::Uuid id, std::string payload, std::string expected,
                   sync::SyncAuthorization original, bool persisted) {
                  if (!owner)
                    return;
                  if (!persisted || !original.Run()) {
                    owner->publish_pending_ = false;
                    owner->blocked_publications_.insert(id);
                    return;
                  }
                  auto found = owner->state_.entries.find(id);
                  if (found == owner->state_.entries.end() ||
                      found->second.pending != payload) {
                    owner->publish_pending_ = false;
                    return;
                  }
                  sync::SyncRecord record;
                  if (!sync::DeserializeRecord(
                          sync::GetEntityType(found->second.record), payload,
                          &record)) {
                    owner->publish_pending_ = false;
                    return;
                  }
                  owner->sync_->PublishWorkspaceStructureIntent(
                      {.record = std::move(record),
                       .expected_payload = expected.empty()
                                               ? std::nullopt
                                               : std::make_optional(expected),
                       .authorization = original},
                      base::BindOnce(&WorkspaceStructureController::OnPublished,
                                     owner, id, payload, original));
                },
                weak_factory_.GetWeakPtr(), id, pending, pending_expected,
                original));
    return;
  }
}

void WorkspaceStructureController::OnPublished(base::Uuid id,
                                               std::string payload,
                                               sync::SyncAuthorization original,
                                               bool success) {
  publish_pending_ = false;
  auto found = state_.entries.find(id);
  if (!success || !original.Run()) {
    blocked_publications_.insert(id);
    return;
  }
  if (found != state_.entries.end() && found->second.pending == payload) {
    auto& entry = found->second;
    sync::SyncRecord committed;
    if (!sync::DeserializeRecord(sync::GetEntityType(entry.record), payload,
                                 &committed)) {
      return;
    }
    entry.record = std::move(committed);
    entry.baseline = payload;
    entry.pending.clear();
    entry.pending_expected.clear();
    dirty_ = true;
  }
  Schedule();
}
}  // namespace ahoi::session
