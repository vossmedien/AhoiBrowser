// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_tree_sync_journal.h"

#include <set>
#include <type_traits>
#include <utility>

#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_field_values.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization_internal.h"
#include "ahoi/browser/sync/sync_store.h"
#include "ahoi/browser/sync/sync_unified_validation.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "ahoi/browser/sync/workspace_structure_sync.h"
#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace ahoi::sync {
namespace {

using Dict = base::DictValue;

HlcStamp SystemBottom() {
  return {.physical_time_us = kMinimumSyncClockPhysicalUs,
          .device_tiebreak = "9e20c6c4-c12a-52ed-b9c5-6e65b49a2d86"};
}

bool IsUnmodifiedSystemInbox(const SyncRecord& record) {
  const auto* workspace = std::get_if<WorkspaceRecord>(&record);
  return workspace &&
         workspace->id.AsLowercaseString() ==
             "83699047-edf8-580d-948d-9c37acc35cb6" &&
         workspace->name == "Inbox" && workspace->icon.empty() &&
         workspace->sort_key == "0" && !workspace->accent_argb &&
         workspace->created_at == base::Time::UnixEpoch() &&
         workspace->modified_at == base::Time::UnixEpoch() &&
         workspace->archive_policy == SharedArchivePolicy::kNever &&
         !workspace->tombstone;
}

std::string Key(const SyncRecord& record) {
  return base::NumberToString(static_cast<int>(GetEntityType(record))) + ":" +
         GetEntityId(record).AsLowercaseString();
}

// Local observation values only, not wire records. Atomic groups exactly match
// FieldEqual/CopyField; no top-clock is invented merely to serialize a
// baseline.
std::optional<Dict> Observation(const SyncRecord& record) {
  if (!IsCanonicalSyncDeviceId(GetEntityId(record).AsLowercaseString())) {
    return std::nullopt;
  }
  Dict result;
  if (const auto* workspace = std::get_if<WorkspaceRecord>(&record)) {
    if (workspace->archive_policy < SharedArchivePolicy::kNever ||
        workspace->archive_policy > SharedArchivePolicy::kThirtyDays)
      return std::nullopt;
    result.Set("archive_policy", static_cast<int>(workspace->archive_policy));
    result.Set("name", workspace->name);
    result.Set("icon", workspace->icon);
    result.Set("sort_key", workspace->sort_key);
    result.Set("accent_argb",
               workspace->accent_argb
                   ? base::Value(base::NumberToString(*workspace->accent_argb))
                   : base::Value());
    serialization_internal::SetTime(result, "created_at",
                                    workspace->created_at);
    serialization_internal::SetTime(result, "modified_at",
                                    workspace->modified_at);
    result.Set("tombstone", workspace->tombstone);
    return result;
  }
  const auto* node = std::get_if<TreeNodeRecord>(&record);
  if (!node || !ValidateHomeTarget(node->home_target) ||
      (node->kind == TreeNodeKind::kPage &&
       !ValidateSharedTarget(node->url, node->target_kind,
                             node->local_scheme)) ||
      (node->kind == TreeNodeKind::kFolder &&
       (!node->url.empty() || node->target_kind || node->local_scheme ||
        node->is_temporary || node->home_target))) {
    return std::nullopt;
  }
  Dict location;
  location.Set("workspace_id", node->workspace_id.AsLowercaseString());
  location.Set("parent_id",
               node->parent_id
                   ? base::Value(node->parent_id->AsLowercaseString())
                   : base::Value());
  location.Set("sort_key", node->sort_key);
  result.Set("location", std::move(location));
  result.Set("kind", static_cast<int>(node->kind));
  result.Set("title", node->title);
  result.Set("icon", node->icon);
  result.Set("accent_argb",
             node->accent_argb
                 ? base::Value(base::NumberToString(*node->accent_argb))
                 : base::Value());
  Dict target;
  target.Set("url", node->url);
  target.Set("target_kind",
             node->target_kind
                 ? base::Value(static_cast<int>(*node->target_kind))
                 : base::Value());
  target.Set("local_scheme", node->local_scheme
                                 ? base::Value(*node->local_scheme)
                                 : base::Value());
  result.Set("url", std::move(target));
  Dict home;
  serialization_internal::SetHomeTarget(home, node->home_target);
  result.Set("home_target", std::move(home));
  serialization_internal::SetTime(result, "created_at", node->created_at);
  serialization_internal::SetTime(result, "modified_at", node->modified_at);
  result.Set("is_temporary", node->is_temporary);
  result.Set("tombstone", node->tombstone);
  return result;
}

std::vector<SyncRecord> Records(const tab_tree::TabTreeSnapshot& tree) {
  std::vector<SyncRecord> result;
  for (const auto& workspace : tree.workspaces) {
    result.emplace_back(WorkspaceToSyncRecord(workspace, {}));
  }
  for (const auto& node : tree.nodes) {
    result.emplace_back(TreeNodeToSyncRecord(node, {}));
  }
  return result;
}

bool Authorized(const SyncAuthorization& authority) {
  return authority && authority.Run();
}

}  // namespace

NativeTreeSyncJournal::NativeTreeSyncJournal(SyncStore* store) : store_(store) {
  CHECK(store_);
}

NativeTreeSyncJournal::~NativeTreeSyncJournal() = default;

bool NativeTreeSyncJournal::ReconcileLocal(
    const NativeTreeSyncSnapshot& native,
    HybridLogicalClock* clock,
    const SyncAuthorization& read_authorization,
    const SyncAuthorization& write_authorization,
    bool* wrote_records) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!clock || !wrote_records || !Authorized(read_authorization)) {
    return false;
  }
  *wrote_records = false;
  Dict baseline;
  std::string last_observation;
  {
    sql::Statement observation(store_->db_.GetUniqueStatement(
        "SELECT payload FROM sync_native_tree_observations WHERE "
        "receipt_id=?"));
    observation.BindString(0, native.baseline_receipt);
    if (observation.Step()) {
      last_observation = observation.ColumnString(0);
    } else if (!observation.Succeeded()) {
      return false;
    }
  }
  if (!last_observation.empty()) {
    auto value =
        base::JSONReader::ReadDict(last_observation, base::JSON_PARSE_RFC);
    if (!value) {
      return false;
    }
    baseline = std::move(*value);
  } else if (!native.baseline_receipt.empty()) {
    sql::Statement receipt(store_->db_.GetUniqueStatement(
        "SELECT payload FROM sync_native_tree_receipts WHERE receipt_id=?"));
    receipt.BindString(0, native.baseline_receipt);
    if (!receipt.Step()) {
      return false;  // Unknown store/receipt: preserve native data, no
                     // guessing.
    }
    auto value = base::JSONReader::ReadDict(receipt.ColumnString(0),
                                            base::JSON_PARSE_RFC);
    if (!value) {
      return false;
    }
    baseline = std::move(*value);
  }
  std::vector<SyncRecord> changes;
  Dict current_observation;
  std::set<std::string> identities;
  for (const auto& local : Records(native.tree)) {
    const auto fields = Observation(local);
    if (!fields || !identities.insert(Key(local)).second) {
      return false;
    }
    current_observation.Set(Key(local), fields->Clone());
    const auto* observed = baseline.FindDict(Key(local));
    SyncRecord previous;
    const auto found =
        store_->GetRecord(GetEntityType(local), GetEntityId(local), &previous);
    if (found != SyncStore::Result::kOk &&
        found != SyncStore::Result::kNotFound) {
      return false;
    }
    if (found == SyncStore::Result::kNotFound) {
      SyncVersion watermark;
      const auto deleted = store_->ReadDeletionWatermark(
          GetEntityType(local), GetEntityId(local), &watermark);
      if (deleted == SyncStore::Result::kOk || observed) {
        continue;  // A missing/compacted shared row is not a new native
                   // creation.
      }
      if (deleted != SyncStore::Result::kNotFound) {
        return false;
      }
    }
    SyncRecord changed = found == SyncStore::Result::kOk ? previous : local;
    bool has_changes = found == SyncStore::Result::kNotFound;
    if (found == SyncStore::Result::kOk && observed) {
      if (observed->size() != fields->size()) {
        return false;
      }
      for (const auto [field, value] : *fields) {
        const auto* original = observed->Find(field);
        if (!original) {
          return false;
        }
        if (*original != value &&
            !field_internal::FieldEqual(local, previous, field)) {
          // A native title edit must not author a stale URL/location that was
          // merely still visible while a remote field changed in the store.
          field_internal::CopyField(local, field, &changed);
          has_changes = true;
        }
      }
    }
    // First attachment with an overlapping existing ID has no native baseline;
    // keep the shared row authoritative instead of comparing whole top-clocks.
    if (!has_changes) {
      continue;
    }
    if (!Authorized(write_authorization)) {
      return false;  // Local intent remains durably in Native Store, not
                     // erased.
    }
    if (found == SyncStore::Result::kOk) {
      clock->Observe(GetVersion(previous).stamp);
    }
    // This exact cross-platform default is system initialization, not a
    // user's edit. Match Swift's canonical Inbox top AND complete field map;
    // a late bootstrap must never outrank somebody's real Inbox customization.
    const bool system_inbox = found == SyncStore::Result::kNotFound &&
                              IsUnmodifiedSystemInbox(changed);
    const auto stamp = system_inbox ? SystemBottom() : clock->Tick();
    std::visit([&stamp](auto& value) { value.version = {.stamp = stamp}; },
               changed);
    if (!StampLocalMutation(
            found == SyncStore::Result::kOk ? &previous : nullptr, &changed)) {
      return false;
    }
    if (found == SyncStore::Result::kNotFound &&
        native.baseline_receipt.empty()) {
      // Initial pre-link native data supplies a creation time value, not proof
      // of its creator/saving device. Never invent a local-device badge.
      const HlcStamp unknown = SystemBottom();
      std::visit(
          [&unknown](auto& value) {
            value.field_versions.insert_or_assign("created_at", unknown);
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                         TreeNodeRecord>) {
              value.field_versions.insert_or_assign("is_temporary", unknown);
            }
          },
          changed);
    }
    changes.push_back(std::move(changed));
  }
  // Native Undo(Create) physically removes a row rather than leaving a native
  // tombstone. Only this COMPLETE, receipt-backed profile tree can attest that
  // deletion. A window/Presence capture never supplies this authority.
  for (const auto [key, value] : baseline) {
    if (identities.contains(key)) {
      continue;
    }
    const auto* observed = value.GetIfDict();
    if (!observed || !observed->FindBool("tombstone")) {
      return false;
    }
    if (*observed->FindBool("tombstone")) {
      continue;
    }
    const size_t separator = key.find(':');
    int raw_type = -1;
    if (separator == std::string::npos ||
        !base::StringToInt(key.substr(0, separator), &raw_type) ||
        (raw_type != static_cast<int>(EntityType::kWorkspace) &&
         raw_type != static_cast<int>(EntityType::kTreeNode))) {
      return false;
    }
    const auto id = base::Uuid::ParseLowercase(key.substr(separator + 1));
    SyncRecord current;
    const auto found =
        store_->GetRecord(static_cast<EntityType>(raw_type), id, &current);
    if (found == SyncStore::Result::kNotFound) {
      continue;
    }
    if (found != SyncStore::Result::kOk) {
      return false;
    }
    if (IsTombstone(current)) {
      continue;
    }
    if (!Authorized(write_authorization)) {
      return false;
    }
    clock->Observe(GetVersion(current).stamp);
    const auto stamp = clock->Tick();
    std::visit(
        [&stamp](auto& record) {
          record.tombstone = true;
          record.version = {.stamp = stamp};
        },
        current);
    changes.push_back(std::move(current));
  }
  if (!Authorized(read_authorization)) {
    return false;
  }
  const auto observed_payload = base::WriteJson(current_observation);
  if (!observed_payload) {
    return false;
  }
  if (changes.empty() && *observed_payload == last_observation) {
    return true;
  }
  sql::Transaction transaction(&store_->db_);
  if (!transaction.Begin()) {
    return false;
  }
  bool changed = false;
  for (const auto& record : changes) {
    const auto result = store_->PutLocalRecordInTransaction(record, {});
    if (result != SyncStore::Result::kOk &&
        result != SyncStore::Result::kAlreadyApplied) {
      return false;
    }
    changed |= result == SyncStore::Result::kOk;
  }
  for (const auto& record : changes) {
    if (!store_->ValidateLocalRecordReferences(record)) {
      return false;
    }
  }
  if (!store_->ValidateLocalBatchGraphs(changes)) {
    return false;
  }
  sql::Statement observed(store_->db_.GetUniqueStatement(
      "INSERT INTO sync_native_tree_observations(receipt_id,payload) "
      "VALUES(?,?) "
      "ON CONFLICT(receipt_id) DO UPDATE SET payload=excluded.payload"));
  observed.BindString(0, native.baseline_receipt);
  observed.BindString(1, *observed_payload);
  if (!observed.Run() || !Authorized(read_authorization) ||
      (!changes.empty() && !Authorized(write_authorization)) ||
      !transaction.Commit()) {
    return false;
  }
  // Native observation, local field mutations and outbox share one commit.
  // Repeated captures cannot re-author an already accepted local intent over a
  // later remote value while native application/readback is still pending.
  if (changed) {
    store_->NotifyChanged();
  }
  *wrote_records = changed;
  return true;
}

std::optional<std::string> NativeTreeSyncJournal::PlanProjection(
    const tab_tree::TabTreeSnapshot& tree,
    const SyncAuthorization& authorization) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!Authorized(authorization)) {
    return std::nullopt;
  }
  Dict observations;
  for (const auto& record : Records(tree)) {
    auto fields = Observation(record);
    if (!fields || observations.contains(Key(record))) {
      return std::nullopt;
    }
    observations.Set(Key(record), std::move(*fields));
  }
  const auto payload = base::WriteJson(observations);
  if (!payload) {
    return std::nullopt;
  }
  // Distinct apply generations may project identical values. Reusing a
  // content-hash ID would also reuse its later native observations and mistake
  // an applied remote value for a new local undo after restart.
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  sql::Transaction transaction(&store_->db_);
  if (!transaction.Begin()) {
    return std::nullopt;
  }
  sql::Statement receipt(store_->db_.GetUniqueStatement(
      "INSERT INTO sync_native_tree_receipts(receipt_id,payload) VALUES(?,?)"));
  receipt.BindString(0, id);
  receipt.BindString(1, *payload);
  if (!receipt.Run() || !Authorized(authorization) || !transaction.Commit()) {
    return std::nullopt;
  }
  // The receipt is prepared before Native apply. Unused receipts are harmless;
  // only the one atomically persisted by Native Store may become its baseline.
  return id;
}

}  // namespace ahoi::sync
