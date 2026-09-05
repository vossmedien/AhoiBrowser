// Copyright 2026 The AhoiBrowser Authors
// Use of this source code is governed by a GPL-3.0-or-later license that can be
// found in the LICENSE file.

#ifndef AHOI_BROWSER_SYNC_SYNC_STORE_H_
#define AHOI_BROWSER_SYNC_SYNC_STORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ahoi/browser/sync/sync_model.h"
#include "base/files/file_path.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "sql/database.h"

namespace sql {
class MetaTable;
class Statement;
}  // namespace sql

namespace ahoi::sync {

class BookmarkSyncJournal;

class SyncStoreObserver : public base::CheckedObserver {
 public:
  ~SyncStoreObserver() override = default;
  virtual void OnSyncStoreChanged() = 0;
};

// Profile-scoped, local-first sync database. All methods are synchronous by
// design and must be called on the sequence that owns this object; a browser
// integration should construct/move it to a dedicated MayBlock sequence. Each
// mutation is one SQLite transaction that includes the record and outbox (or
// inbox/token) side effects.
class SyncStore {
 public:
  enum class Result {
    kOk = 0,
    kNotInitialized,
    kInvalidArgument,
    kNotFound,
    kAlreadyApplied,
    kStale,
    kConflict,
    kDatabaseError,
  };

  static constexpr int kCurrentSchemaVersion =
      ::ahoi::sync::kCurrentSchemaVersion;
  static constexpr int kLowestSupportedSchemaVersion = 1;

  SyncStore();
  SyncStore(const SyncStore&) = delete;
  SyncStore& operator=(const SyncStore&) = delete;
  ~SyncStore();

  [[nodiscard]] bool Initialize(const base::FilePath& path);
  [[nodiscard]] bool InitializeInMemory();

  void AddObserver(SyncStoreObserver* observer);
  void RemoveObserver(SyncStoreObserver* observer);

  // The caller supplies a fresh HLC version. If mutation_id is empty, a new
  // UUID is generated. Calling this again with the same mutation id is safe and
  // returns kAlreadyApplied rather than adding a second outbox entry.
  [[nodiscard]] Result PutLocalRecord(const SyncRecord& record,
                                      std::string mutation_id = {});

  // Applies an entire provider page atomically. A repeated mutation is a
  // no-op, a stale version is retained in the inbox but cannot overwrite the
  // current row, and the change token/retry reset commit with the page.
  [[nodiscard]] Result ApplyRemoteBatch(const ProviderBatch& batch);

  [[nodiscard]] Result GetRecord(EntityType type,
                                 const base::Uuid& id,
                                 SyncRecord* record) const;
  [[nodiscard]] Result GetRecords(EntityType type,
                                  std::vector<SyncRecord>* records) const;
  // Includes tombstones because merge/restart code needs them. Consumers that
  // render tabs should use DeviceTabsService, which filters them and all
  // incognito rows before publishing a snapshot.
  [[nodiscard]] Result GetRemoteTabs(
      std::vector<RemoteTabRecord>* records) const;

  // Category filtering happens before the limit and never changes queued rows.
  [[nodiscard]] Result ReadOutbox(size_t limit,
                                  std::vector<SyncChange>* changes,
                                  bool include_bookmarks = true) const;
  [[nodiscard]] Result AcknowledgeOutbox(
      const std::vector<std::string>& mutation_ids);
  // Rebuilds transport work without changing the canonical records. Passing
  // false is the account-privacy choice; true republishes every retained
  // record after an explicitly confirmed account or custom-zone recovery.
  [[nodiscard]] Result PrepareOutboxForCloudRecovery(
      bool requeue_local_records);
  [[nodiscard]] int64_t PendingOutboxCount() const;
  [[nodiscard]] int64_t InboxCount() const;
  [[nodiscard]] int64_t QuarantineCount() const;

  // Physically compacts only tombstones older than the policy window and only
  // after their outbox mutation has been acknowledged. A durable deletion
  // watermark remains, preventing a delayed provider page from resurrecting
  // the entity. No CloudKit physical delete is issued by this operation.
  [[nodiscard]] Result CompactExpiredTombstones(base::Time now,
                                                base::TimeDelta retention);

  [[nodiscard]] std::string GetChangeToken() const;
  [[nodiscard]] RetryState GetRetryState() const;
  [[nodiscard]] Result MarkRetry(base::Time next_attempt, std::string error);
  [[nodiscard]] Result ClearRetry();

  // Atomically claims both the command identity and the source-scoped nonce.
  // Expired rows are pruned in the same transaction. A crash after a claim is
  // intentionally at-most-once: the command is not executed again after
  // restart, even if an acknowledgement was not yet uploaded.
  [[nodiscard]] Result ConsumeRemoteCommand(const base::Uuid& command_id,
                                            const base::Uuid& source_device_id,
                                            std::string nonce_base64,
                                            base::Time expires_at,
                                            base::Time now);

 private:
  friend class BookmarkSyncJournal;
  friend class BookmarkSyncAuthorizationTest;

  struct StoredRecord {
    SyncRecord record;
    std::string payload;
  };

  [[nodiscard]] bool InitializeSchema();
  [[nodiscard]] bool CreateSchema();
  [[nodiscard]] bool MigrateSchema(sql::MetaTable* meta_table);
  [[nodiscard]] bool IsReady() const;

  [[nodiscard]] Result ReadStoredRecord(EntityType type,
                                        const base::Uuid& id,
                                        StoredRecord* stored) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool ReadStoredRecords(EntityType type,
                                       std::vector<StoredRecord>* records) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool UpsertRecord(const SyncRecord& record,
                                  const std::string& payload)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool WriteTombstone(const SyncRecord& record)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool WriteOutbox(const SyncChange& change)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool OutboxMatches(const SyncChange& change, bool* present)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool WriteInbox(const SyncChange& change)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool InboxMatches(const SyncChange& change, bool* present)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool WriteQuarantine(const SyncChange& change,
                                     const std::string& reason)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] Result ReadDeletionWatermark(EntityType type,
                                             const base::Uuid& id,
                                             SyncVersion* version) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  [[nodiscard]] bool SetMetadata(const std::string& key,
                                 const std::string& value)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  void NotifyChanged();

  mutable sql::Database db_ GUARDED_BY_CONTEXT(sequence_checker_);
  base::ObserverList<SyncStoreObserver> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_SYNC_STORE_H_
