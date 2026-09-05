// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_journal.h"

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/check.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/stringprintf.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

bool SameLocation(const BookmarkRecord& a, const BookmarkRecord& b) {
  return a.root_kind == b.root_kind && a.parent_id == b.parent_id &&
         a.sort_key == b.sort_key;
}

bool SameContent(const BookmarkRecord& a, const BookmarkRecord& b) {
  return a.id == b.id && a.kind == b.kind && SameLocation(a, b) &&
         a.title == b.title && a.url == b.url && a.created_at == b.created_at &&
         a.tombstone == b.tombstone;
}

}  // namespace

BookmarkSyncJournal::BookmarkSyncJournal(SyncStore* store) : store_(store) {
  CHECK(store_);
}

BookmarkSyncJournal::~BookmarkSyncJournal() = default;

bool BookmarkSyncJournal::LoadBindings(Bindings* bindings) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  sql::Statement statement(store_->db_.GetUniqueStatement(
      "SELECT native_key,entity_id,baseline,native_index,materialized,"
      "last_observed,observed_receipt,observation_session "
      "FROM sync_bookmark_bindings"));
  while (statement.Step()) {
    const std::string key = statement.ColumnString(0);
    base::Uuid native_uuid;
    bool account = false;
    Binding binding{.id =
                        base::Uuid::ParseLowercase(statement.ColumnString(1))};
    const int64_t index = statement.ColumnInt64(3);
    const int materialized = statement.ColumnInt(4);
    if (!ParseNativeBookmarkKey(key, &native_uuid, &account) ||
        !binding.id.is_valid() || index < 0 ||
        (materialized != 0 && materialized != 1)) {
      return false;
    }
    const std::string payload = statement.ColumnString(2);
    if (!payload.empty()) {
      SyncRecord decoded;
      if (!DeserializeRecord(EntityType::kBookmark, payload, &decoded) ||
          GetEntityId(decoded) != binding.id) {
        return false;
      }
      binding.baseline = std::get<BookmarkRecord>(std::move(decoded));
    }
    binding.index = base::checked_cast<size_t>(index);
    binding.materialized = materialized != 0;
    const auto last_observed = statement.ColumnString(5);
    if (!last_observed.empty()) {
      SyncRecord decoded;
      if (!DeserializeRecord(EntityType::kBookmark, last_observed, &decoded) ||
          GetEntityId(decoded) != binding.id) {
        return false;
      }
      binding.last_observed = std::get<BookmarkRecord>(std::move(decoded));
    }
    binding.observed_receipt = statement.ColumnString(6);
    binding.observation_session = statement.ColumnString(7);
    if (!bindings->emplace(key, std::move(binding)).second) {
      return false;
    }
  }
  return statement.Succeeded();
}

bool BookmarkSyncJournal::WriteBinding(const std::string& key,
                                       const Binding& binding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  std::string payload;
  if (binding.baseline && !SerializeRecord(*binding.baseline, &payload)) {
    return false;
  }
  std::string last_observed;
  if (binding.last_observed &&
      !SerializeRecord(*binding.last_observed, &last_observed)) {
    return false;
  }
  sql::Statement statement(store_->db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO sync_bookmark_bindings "
      "(native_key,entity_id,baseline,native_index,materialized,last_observed,"
      "observed_receipt,observation_session) VALUES(?,?,?,?,?,?,?,?)"));
  statement.BindString(0, key);
  statement.BindString(1, binding.id.AsLowercaseString());
  statement.BindString(2, payload);
  statement.BindInt64(3, base::checked_cast<int64_t>(binding.index));
  statement.BindInt(4, binding.materialized ? 1 : 0);
  statement.BindString(5, last_observed);
  statement.BindString(6, binding.observed_receipt);
  statement.BindString(7, binding.observation_session);
  return statement.Run();
}

bool BookmarkSyncJournal::ResolveBindings(
    const NativeBookmarkSnapshot& snapshot,
    Bindings* bindings) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  std::set<std::string> present;
  for (const auto& entry : snapshot.entries) {
    base::Uuid native_uuid;
    bool account = false;
    if (!ParseNativeBookmarkKey(entry.native_key, &native_uuid, &account) ||
        !present.insert(entry.native_key).second) {
      return false;
    }
  }
  std::set<base::Uuid> logical_ids;
  for (const auto& entry : snapshot.entries) {
    auto found = bindings->find(entry.native_key);
    if (entry.previous_native_key &&
        *entry.previous_native_key != entry.native_key) {
      if (present.contains(*entry.previous_native_key)) {
        return false;
      }
      const auto old = bindings->find(*entry.previous_native_key);
      if (old != bindings->end()) {
        if (found != bindings->end() && found->second.id != old->second.id) {
          return false;
        }
        // Retain the old key as a recovery alias if native disk persistence
        // completes after the ledger transaction or a native move is undone.
        found = bindings->insert_or_assign(entry.native_key, old->second).first;
      }
    }
    if (found == bindings->end()) {
      found =
          bindings
              ->emplace(entry.native_key,
                        Binding{.id = InitialBookmarkSyncId(entry.native_key)})
              .first;
    }
    if (entry.explicitly_added) {
      SyncRecord previous;
      const auto stored =
          store_->GetRecord(EntityType::kBookmark, found->second.id, &previous);
      SyncVersion watermark;
      const auto compacted = store_->ReadDeletionWatermark(
          EntityType::kBookmark, found->second.id, &watermark);
      if ((stored == SyncStore::Result::kOk && IsTombstone(previous)) ||
          compacted == SyncStore::Result::kOk) {
        // An explicit native undo after sync deletion is a new logical item,
        // not resurrection of the old identity or its deletion watermark.
        found->second = Binding{.id = base::Uuid::GenerateRandomV4()};
      } else if ((stored != SyncStore::Result::kOk &&
                  stored != SyncStore::Result::kNotFound) ||
                 compacted != SyncStore::Result::kNotFound) {
        return false;
      }
    }
    if (!found->second.id.is_valid() ||
        !logical_ids.insert(found->second.id).second) {
      return false;
    }
  }
  return true;
}

bool BookmarkSyncJournal::ProjectNative(
    const NativeBookmarkSnapshot& snapshot,
    const Bindings& bindings,
    std::map<std::string, BookmarkRecord>* records) const {
  std::map<std::string, std::vector<const NativeBookmarkEntry*>> groups;
  for (const auto& entry : snapshot.entries) {
    const auto binding = bindings.find(entry.native_key);
    if (binding == bindings.end() ||
        entry.root.has_value() == entry.parent_key.has_value()) {
      return false;
    }
    BookmarkRecord record{.id = binding->second.id,
                          .kind = entry.kind,
                          .root_kind = entry.root,
                          .title = entry.title,
                          .url = entry.url,
                          .created_at = entry.created_at};
    if (entry.parent_key) {
      const auto parent = bindings.find(*entry.parent_key);
      if (parent == bindings.end()) {
        return false;
      }
      record.parent_id = parent->second.id;
    }
    if (binding->second.baseline) {
      record.sort_key = binding->second.baseline->sort_key;
      record.version = binding->second.baseline->version;
    } else {
      record.version.stamp = {.physical_time_us = 0,
                              .device_tiebreak = "native"};
    }
    const std::string group = entry.parent_key.value_or(
        "root:" + std::to_string(static_cast<int>(
                      entry.root.value_or(BookmarkRoot::kBookmarkBar))));
    groups[group].push_back(&entry);
    records->emplace(entry.native_key, std::move(record));
  }
  for (auto& [group, entries] : groups) {
    std::ranges::sort(entries, {}, &NativeBookmarkEntry::index);
    std::vector<size_t> next_known(entries.size() + 1, entries.size());
    std::vector<std::string> suffix_max(entries.size() + 1);
    for (size_t i = entries.size(); i > 0; --i) {
      const auto& key = records->at(entries[i - 1]->native_key).sort_key;
      next_known[i - 1] = key.empty() ? next_known[i] : i - 1;
      suffix_max[i - 1] = std::max(key, suffix_max[i]);
    }
    std::string lower;
    bool rebalance = false;
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i && entries[i - 1]->index == entries[i]->index) {
        return false;
      }
      auto& record = records->at(entries[i]->native_key);
      if (record.sort_key > lower) {
        lower = record.sort_key;
        continue;
      }
      std::optional<std::string> upper;
      if (suffix_max[i + 1] > lower) {
        for (size_t next = next_known[i + 1]; next < entries.size();
             next = next_known[next + 1]) {
          const auto& key = records->at(entries[next]->native_key).sort_key;
          if (key > lower) {
            upper = key;
            break;
          }
        }
      }
      const auto key = BookmarkSortKeyBetween(lower, upper);
      if (!key) {
        rebalance = true;
        break;
      }
      record.sort_key = lower = *key;
    }
    if (rebalance) {
      for (size_t i = 0; i < entries.size(); ++i) {
        records->at(entries[i]->native_key).sort_key =
            base::StringPrintf("%016llx", static_cast<unsigned long long>(i));
      }
    }
  }
  std::vector<BookmarkRecord> graph;
  for (const auto& [key, record] : *records) {
    graph.push_back(record);
  }
  return ValidateBookmarkGraph(graph);
}

bool BookmarkSyncJournal::WriteChangedRecord(BookmarkRecord record,
                                             HybridLogicalClock* clock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  SyncStore::StoredRecord previous;
  const auto found =
      store_->ReadStoredRecord(EntityType::kBookmark, record.id, &previous);
  if (found != SyncStore::Result::kOk &&
      found != SyncStore::Result::kNotFound) {
    return false;
  }
  SyncVersion watermark;
  const auto deleted = store_->ReadDeletionWatermark(EntityType::kBookmark,
                                                     record.id, &watermark);
  if (deleted == SyncStore::Result::kOk) {
    return true;  // Native stale state cannot resurrect a compacted deletion.
  }
  if (deleted != SyncStore::Result::kNotFound) {
    return false;
  }
  if (found == SyncStore::Result::kOk) {
    if (IsTombstone(previous.record) ||
        SameContent(std::get<BookmarkRecord>(previous.record), record)) {
      return true;
    }
    clock->Observe(GetVersion(previous.record).stamp, base::Time::Now());
  }
  record.version = {.stamp = clock->Tick(base::Time::Now())};
  SyncRecord stamped = std::move(record);
  if (!StampLocalMutation(
          found == SyncStore::Result::kOk ? &previous.record : nullptr,
          &stamped) ||
      !ValidateRecord(stamped)) {
    return false;
  }
  std::string payload;
  if (!SerializeRecord(stamped, &payload)) {
    return false;
  }
  const SyncChange change{
      .mutation_id = base::Uuid::GenerateRandomV4().AsLowercaseString(),
      .entity_type = EntityType::kBookmark,
      .entity_id = GetEntityId(stamped),
      .kind = IsTombstone(stamped) ? ChangeKind::kDelete : ChangeKind::kUpsert,
      .version = GetVersion(stamped),
      .payload = payload};
  return store_->UpsertRecord(stamped, payload) &&
         store_->WriteTombstone(stamped) && store_->WriteOutbox(change);
}

std::optional<BookmarkSyncProjection> BookmarkSyncJournal::ReconcileLocal(
    const NativeBookmarkSnapshot& snapshot,
    HybridLogicalClock* clock,
    const BookmarkSyncAuthorization& authorization) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!clock || !store_->IsReady() || snapshot.local_data_blocked ||
      (authorization && !authorization.Run())) {
    return std::nullopt;
  }
  Bindings bindings;
  std::map<std::string, BookmarkRecord> native;
  if (!LoadBindings(&bindings) || !ResolveBindings(snapshot, &bindings)) {
    return std::nullopt;
  }
  Bindings effective = bindings;
  if (!LoadEffectiveBaselines(snapshot, &effective) ||
      !ProjectNative(snapshot, effective, &native)) {
    return std::nullopt;
  }
  sql::Transaction transaction(&store_->db_);
  if (!transaction.Begin()) {
    return std::nullopt;
  }
  std::map<std::string, const NativeBookmarkEntry*> native_entries;
  for (const auto& entry : snapshot.entries) {
    native_entries.emplace(entry.native_key, &entry);
  }
  for (const auto& [key, observed] : native) {
    Binding& binding = bindings.at(key);
    SyncRecord stored;
    const auto result =
        store_->GetRecord(EntityType::kBookmark, binding.id, &stored);
    BookmarkRecord next = observed;
    if (result == SyncStore::Result::kOk) {
      next = std::get<BookmarkRecord>(stored);
      if (effective.at(key).baseline) {
        const auto& before = *effective.at(key).baseline;
        if (observed.title != before.title) {
          next.title = observed.title;
        }
        if (observed.url != before.url) {
          next.url = observed.url;
        }
        if (observed.created_at != before.created_at) {
          next.created_at = observed.created_at;
        }
        if (!SameLocation(observed, before)) {
          next.parent_id = observed.parent_id;
          next.root_kind = observed.root_kind;
          next.sort_key = observed.sort_key;
        }
        if (observed.kind != before.kind) {
          return std::nullopt;
        }
      }
    } else if (result != SyncStore::Result::kNotFound) {
      return std::nullopt;
    }
    if (!WriteChangedRecord(next, clock)) {
      return std::nullopt;
    }
    // Retain the original no-receipt baseline. A native receipt is atomically
    // saved with its fields; overwriting this fallback on an in-memory apply
    // would republish old disk state after a crash before Chromium's save.
    if (!binding.baseline && !effective.at(key).baseline) {
      binding.baseline = observed;
    }
    binding.index = native_entries.at(key)->index;
    binding.materialized = true;
    binding.last_observed = observed;
    binding.observed_receipt = native_entries.at(key)->apply_receipt;
    binding.observation_session = snapshot.observation_session;
    if (!WriteBinding(key, binding)) {
      return std::nullopt;
    }
  }
  for (const auto& key : snapshot.removed_keys) {
    if (native.contains(key)) {
      continue;  // Removed then undone before capture.
    }
    const auto found = bindings.find(key);
    if (found == bindings.end()) {
      continue;
    }
    if (std::ranges::any_of(native, [&](const auto& item) {
          return item.second.id == found->second.id;
        })) {
      continue;  // Native storage move, not deletion.
    }
    SyncRecord existing;
    const auto result =
        store_->GetRecord(EntityType::kBookmark, found->second.id, &existing);
    if (result == SyncStore::Result::kNotFound) {
      continue;
    }
    if (result != SyncStore::Result::kOk) {
      return std::nullopt;
    }
    auto deleted = std::get<BookmarkRecord>(existing);
    deleted.tombstone = true;
    if (!WriteChangedRecord(deleted, clock)) {
      return std::nullopt;
    }
  }
  std::vector<SyncRecord> combined;
  if (store_->GetRecords(EntityType::kBookmark, &combined) !=
      SyncStore::Result::kOk) {
    return std::nullopt;
  }
  std::vector<BookmarkRecord> combined_graph;
  for (const auto& record : combined) {
    combined_graph.push_back(std::get<BookmarkRecord>(record));
  }
  // Recheck at the commit boundary too: a provider event can revoke scope
  // while this backend sequence is assembling the candidate. Roll back all
  // tentative bindings/records/outbox changes rather than confirming them.
  if (!ValidateBookmarkGraph(combined_graph) ||
      (authorization && !authorization.Run()) || !transaction.Commit()) {
    return std::nullopt;
  }
  store_->NotifyChanged();
  return ReadProjection(authorization);
}

bool BookmarkSyncJournal::AcknowledgeNativeProjection(
    const NativeBookmarkSnapshot& snapshot,
    const BookmarkSyncAuthorization& authorization) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!store_->IsReady() || snapshot.local_data_blocked ||
      (authorization && !authorization.Run())) {
    return false;
  }
  Bindings bindings;
  std::map<std::string, BookmarkRecord> projected;
  if (!LoadBindings(&bindings) || !ResolveBindings(snapshot, &bindings)) {
    return false;
  }
  Bindings effective = bindings;
  if (!LoadEffectiveBaselines(snapshot, &effective) ||
      !ProjectNative(snapshot, effective, &projected)) {
    return false;
  }
  sql::Transaction transaction(&store_->db_);
  if (!transaction.Begin()) {
    return false;
  }
  for (const auto& entry : snapshot.entries) {
    auto& binding = bindings.at(entry.native_key);
    SyncRecord stored;
    if (store_->GetRecord(EntityType::kBookmark, binding.id, &stored) !=
        SyncStore::Result::kOk) {
      return false;
    }
    // Accept the exact planned native intent even if a newer peer record has
    // arrived meanwhile. Receipt selection never changes the stored domain.
    auto receipt = ReceiptBaseline(entry.apply_receipt, binding.id);
    if (!entry.apply_receipt.empty() && !receipt) {
      return false;
    }
    auto baseline = receipt ? *receipt : std::get<BookmarkRecord>(stored);
    if (baseline.tombstone || baseline.kind != entry.kind ||
        baseline.title != entry.title ||
        GURL(baseline.url) != GURL(entry.url) ||
        baseline.created_at != entry.created_at) {
      return false;
    }
    // BookmarkModel canonicalizes URLs. Remember exactly what was applied
    // without republishing a peer's equivalent spelling as a fresh edit.
    baseline.url = entry.url;
    baseline.parent_id.reset();
    baseline.root_kind = entry.root;
    if (entry.parent_key) {
      const auto parent = bindings.find(*entry.parent_key);
      if (parent == bindings.end()) {
        return false;
      }
      baseline.parent_id = parent->second.id;
    }
    if (!ValidateRecord(baseline)) {
      return false;
    }
    binding.index = entry.index;
    binding.materialized = true;
    binding.last_observed = std::move(baseline);
    binding.observed_receipt = entry.apply_receipt;
    binding.observation_session = snapshot.observation_session;
    if (!WriteBinding(entry.native_key, binding)) {
      return false;
    }
  }
  return (!authorization || authorization.Run()) && transaction.Commit();
}

std::optional<BookmarkSyncProjection> BookmarkSyncJournal::ReadProjection(
    const BookmarkSyncAuthorization& authorization) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!store_->IsReady() || (authorization && !authorization.Run())) {
    return std::nullopt;
  }
  Bindings bindings;
  std::vector<SyncRecord> records;
  if (!LoadBindings(&bindings) ||
      store_->GetRecords(EntityType::kBookmark, &records) !=
          SyncStore::Result::kOk) {
    return std::nullopt;
  }
  BookmarkSyncProjection result;
  for (const auto& record : records) {
    result.records.push_back(std::get<BookmarkRecord>(record));
  }
  if (!ValidateBookmarkGraph(result.records)) {
    return std::nullopt;
  }
  std::set<base::Uuid> mapped;
  sql::Transaction transaction(&store_->db_);
  if (!transaction.Begin()) {
    return std::nullopt;
  }
  std::map<base::Uuid, std::string> receipts;
  for (const auto& record : result.records) {
    if (record.tombstone) {
      continue;
    }
    const auto receipt = PlanReceipt(record);
    if (!receipt) {
      return std::nullopt;
    }
    receipts.emplace(record.id, *receipt);
  }
  for (const auto& [key, binding] : bindings) {
    // Recovery aliases are also returned. The UI resolves existing native
    // nodes first and rejects two simultaneous nodes claiming one identity.
    result.bindings.push_back({binding.id, key, receipts[binding.id]});
    mapped.insert(binding.id);
  }
  for (const auto& record : result.records) {
    if (record.tombstone || mapped.contains(record.id)) {
      continue;
    }
    std::string key = NativeBookmarkKey(record.id, false);
    // The peer's logical UUID might already be a different native bookmark's
    // GUID. Reserve a different local GUID before any native mutation.
    while (bindings.contains(key)) {
      key = NativeBookmarkKey(base::Uuid::GenerateRandomV4(), false);
    }
    Binding binding{.id = record.id};
    if (!WriteBinding(key, binding)) {
      return std::nullopt;
    }
    bindings.emplace(key, binding);
    result.bindings.push_back({record.id, key, receipts.at(record.id)});
  }
  return (!authorization || authorization.Run()) && transaction.Commit()
             ? std::optional(std::move(result))
             : std::nullopt;
}

}  // namespace ahoi::sync
