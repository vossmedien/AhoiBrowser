// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_journal.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "sql/statement.h"

namespace ahoi::sync {

std::optional<BookmarkRecord> BookmarkSyncJournal::ReceiptBaseline(
    const std::string& receipt,
    const base::Uuid& id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  if (!base::Uuid::ParseLowercase(receipt).is_valid()) {
    return std::nullopt;
  }
  sql::Statement query(store_->db_.GetUniqueStatement(
      "SELECT payload FROM sync_bookmark_apply_receipts "
      "WHERE receipt_id=? AND entity_id=?"));
  query.BindString(0, receipt);
  query.BindString(1, id.AsLowercaseString());
  if (!query.Step()) {
    return std::nullopt;
  }
  SyncRecord decoded;
  if (!DeserializeRecord(EntityType::kBookmark, query.ColumnString(0),
                         &decoded) ||
      GetEntityId(decoded) != id) {
    return std::nullopt;
  }
  return std::get<BookmarkRecord>(std::move(decoded));
}

bool BookmarkSyncJournal::LoadEffectiveBaselines(
    const NativeBookmarkSnapshot& snapshot,
    Bindings* bindings) const {
  for (const auto& entry : snapshot.entries) {
    const auto found = bindings->find(entry.native_key);
    if (found == bindings->end()) {
      return false;
    }
    // Native MetaInfo may be cloned or imported. It only selects a baseline
    // after the independent local identity mapping has already been resolved.
    if (auto baseline =
            ReceiptBaseline(entry.apply_receipt, found->second.id)) {
      found->second.baseline = std::move(baseline);
    }
    auto& binding = found->second;
    if (!binding.last_observed ||
        binding.observed_receipt != entry.apply_receipt) {
      continue;
    }
    const auto& seen = *binding.last_observed;
    const bool same_session =
        !snapshot.observation_session.empty() &&
        binding.observation_session == snapshot.observation_session;
    std::optional<base::Uuid> parent;
    if (entry.parent_key) {
      const auto found_parent = bindings->find(*entry.parent_key);
      if (found_parent == bindings->end()) {
        return false;
      }
      parent = found_parent->second.id;
    }
    const bool same_observation =
        seen.kind == entry.kind && seen.title == entry.title &&
        seen.url == entry.url && seen.created_at == entry.created_at &&
        seen.root_kind == entry.root && seen.parent_id == parent &&
        binding.index == entry.index;
    // In-process reversals are real edits. On restart, old disk content equal
    // to its receipt baseline is not. A repeated already-journalled snapshot
    // also must not reauthor a field over a newer remote mutation.
    if (same_session || same_observation) {
      binding.baseline = seen;
    }
  }
  return true;
}

std::optional<std::string> BookmarkSyncJournal::PlanReceipt(
    const BookmarkRecord& record) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(store_->sequence_checker_);
  std::string payload;
  if (!SerializeRecord(record, &payload)) {
    return std::nullopt;
  }
  sql::Statement existing(store_->db_.GetUniqueStatement(
      "SELECT receipt_id FROM sync_bookmark_apply_receipts "
      "WHERE entity_id=? AND payload=? LIMIT 1"));
  existing.BindString(0, record.id.AsLowercaseString());
  existing.BindString(1, payload);
  if (existing.Step()) {
    const auto token = existing.ColumnString(0);
    return base::Uuid::ParseLowercase(token).is_valid() ? std::optional(token)
                                                        : std::nullopt;
  }
  if (!existing.Succeeded()) {
    return std::nullopt;
  }
  const std::string token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  sql::Statement insert(store_->db_.GetUniqueStatement(
      "INSERT INTO sync_bookmark_apply_receipts "
      "(receipt_id,entity_id,payload) VALUES(?,?,?)"));
  insert.BindString(0, token);
  insert.BindString(1, record.id.AsLowercaseString());
  insert.BindString(2, payload);
  return insert.Run() ? std::optional(token) : std::nullopt;
}

}  // namespace ahoi::sync
