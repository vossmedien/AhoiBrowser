// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/bookmark_sync_journal.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_merge.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

using Result = SyncStore::Result;

constexpr char kLocalDevice[] = "c2000000-0000-4000-8000-000000000001";
constexpr char kPeerDevice[] = "c2000000-0000-4000-8000-000000000002";
constexpr char kObservationSession[] = "c3000000-0000-4000-8000-000000000001";
constexpr char kRestartSession[] = "c3000000-0000-4000-8000-000000000002";
constexpr int64_t kCreatedAt = 11644473601000000LL;

base::Uuid Id(unsigned value) {
  return base::Uuid::ParseLowercase(
      base::StringPrintf("c1000000-0000-4000-8000-%012x", value));
}

std::string Key(unsigned value) {
  return NativeBookmarkKey(Id(value), false);
}

NativeBookmarkEntry Page(unsigned value, size_t index = 0) {
  return {.native_key = Key(value),
          .root = BookmarkRoot::kOther,
          .kind = BookmarkKind::kUrl,
          .index = index,
          .title = base::StringPrintf("Native page %u", value),
          .url = base::StringPrintf("https://example.test/bookmark/%u", value),
          .created_at = base::Time::FromDeltaSinceWindowsEpoch(
              base::Microseconds(kCreatedAt + value))};
}

NativeBookmarkSnapshot Snapshot(std::vector<NativeBookmarkEntry> entries,
                                const char* session = kObservationSession,
                                std::vector<std::string> removed = {}) {
  return {.entries = std::move(entries),
          .removed_keys = std::move(removed),
          .observation_session = session};
}

BookmarkNativeBinding BindingFor(const BookmarkSyncProjection& projection,
                                 const std::string& key) {
  const auto found = std::ranges::find(projection.bindings, key,
                                       &BookmarkNativeBinding::native_key);
  EXPECT_NE(projection.bindings.end(), found);
  return found == projection.bindings.end() ? BookmarkNativeBinding() : *found;
}

// Models a complete native Apply of one top-level node. The receipt must come
// from the journal's committed plan, never from a fabricated metadata value.
NativeBookmarkEntry AppliedPage(const BookmarkSyncProjection& projection,
                                const std::string& key,
                                size_t index = 0) {
  const auto binding = BindingFor(projection, key);
  const auto found = std::ranges::find(projection.records, binding.logical_id,
                                       &BookmarkRecord::id);
  EXPECT_NE(projection.records.end(), found);
  EXPECT_FALSE(binding.apply_receipt.empty());
  if (found == projection.records.end()) {
    return {};
  }
  EXPECT_TRUE(found->root_kind);
  EXPECT_FALSE(found->tombstone);
  return {.native_key = key,
          .root = found->root_kind,
          .kind = found->kind,
          .index = index,
          .title = found->title,
          .url = found->url,
          .created_at = found->created_at,
          .apply_receipt = binding.apply_receipt};
}

BookmarkRecord ReadBookmark(SyncStore& store, const base::Uuid& id) {
  SyncRecord value;
  EXPECT_EQ(Result::kOk, store.GetRecord(EntityType::kBookmark, id, &value));
  const auto* record = std::get_if<BookmarkRecord>(&value);
  EXPECT_NE(nullptr, record);
  return record ? *record : BookmarkRecord();
}

std::vector<SyncChange> ReadOutbox(SyncStore& store) {
  std::vector<SyncChange> changes;
  EXPECT_EQ(Result::kOk, store.ReadOutbox(100, &changes));
  return changes;
}

void AcknowledgeOutbox(SyncStore& store) {
  std::vector<std::string> ids;
  for (const auto& change : ReadOutbox(store)) {
    ids.push_back(change.mutation_id);
  }
  ASSERT_EQ(Result::kOk, store.AcknowledgeOutbox(ids));
}

SyncChange ChangeFor(const BookmarkRecord& record, const char* mutation) {
  std::string payload;
  EXPECT_TRUE(SerializeRecord(record, &payload));
  return {.mutation_id = mutation,
          .entity_type = EntityType::kBookmark,
          .entity_id = record.id,
          .kind = record.tombstone ? ChangeKind::kDelete : ChangeKind::kUpsert,
          .version = record.version,
          .payload = std::move(payload)};
}

BookmarkRecord RemotePage(unsigned value) {
  const auto native = Page(value);
  BookmarkRecord record{
      .id = Id(value),
      .kind = native.kind,
      .root_kind = native.root,
      .sort_key = "M",
      .title = native.title,
      .url = native.url,
      .created_at = native.created_at,
      .version = {.stamp = {.physical_time_us = kCreatedAt + 100,
                            .device_tiebreak = kPeerDevice}}};
  SyncRecord stamped = std::move(record);
  EXPECT_TRUE(StampLocalMutation(nullptr, &stamped));
  return std::get<BookmarkRecord>(stamped);
}

BookmarkRecord PeerRename(BookmarkRecord record) {
  record.title = "Renamed on mobile 海";
  record.version.stamp.physical_time_us += 1000000;
  record.version.stamp.logical = 0;
  record.version.stamp.device_tiebreak = kPeerDevice;
  record.field_versions.insert_or_assign("title", record.version.stamp);
  return record;
}

BookmarkRecord PeerUrl(BookmarkRecord record, const char* url) {
  record.url = url;
  record.version.stamp.physical_time_us += 1000000;
  record.version.stamp.logical = 0;
  record.version.stamp.device_tiebreak = kPeerDevice;
  record.field_versions.insert_or_assign("url", record.version.stamp);
  return record;
}

class BookmarkSyncRecoveryTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(store_.InitializeInMemory()); }

  SyncStore store_;
  BookmarkSyncJournal journal_{&store_};
  HybridLogicalClock clock_{kLocalDevice};
};

TEST(BookmarkSyncRecoveryRestartTest,
     CrashBeforeNativeSaveCannotRepublishTheOldTitle) {
  for (const bool disk_has_receipt : {false, true}) {
    SCOPED_TRACE(disk_has_receipt);
    base::ScopedTempDir directory;
    ASSERT_TRUE(directory.CreateUniqueTempDir());
    const auto path = directory.GetPath().AppendASCII("sync.sqlite");
    NativeBookmarkEntry native_disk = Page(1);
    base::Uuid id;
    BookmarkRecord expected;
    {
      SyncStore store;
      ASSERT_TRUE(store.Initialize(path));
      BookmarkSyncJournal journal(&store);
      HybridLogicalClock clock(kLocalDevice);
      const auto seeded =
          journal.ReconcileLocal(Snapshot({native_disk}), &clock);
      ASSERT_TRUE(seeded);
      id = BindingFor(*seeded, native_disk.native_key).logical_id;
      const auto first_apply = AppliedPage(*seeded, native_disk.native_key);
      ASSERT_TRUE(journal.AcknowledgeNativeProjection(Snapshot({first_apply})));
      if (disk_has_receipt) {
        native_disk = first_apply;
      }
      AcknowledgeOutbox(store);
      const auto peer = PeerRename(ReadBookmark(store, id));
      ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                                 {.changes = {ChangeFor(peer, "peer-title")}}));
      const auto projection = journal.ReadProjection();
      ASSERT_TRUE(projection);
      const auto applied = AppliedPage(*projection, native_disk.native_key);
      ASSERT_NE(native_disk.title, applied.title);
      ASSERT_NE(first_apply.apply_receipt, applied.apply_receipt);
      ASSERT_TRUE(journal.AcknowledgeNativeProjection(Snapshot({applied})));
      expected = ReadBookmark(store, id);
      EXPECT_EQ(0, store.PendingOutboxCount());
      // SQLite is durable, but BookmarkModel's delayed JSON save has not
      // replaced native_disk. Reopening only the journal models this crash.
    }
    {
      SyncStore store;
      ASSERT_TRUE(store.Initialize(path));
      BookmarkSyncJournal journal(&store);
      HybridLogicalClock clock(kLocalDevice);
      const auto restored = journal.ReconcileLocal(
          Snapshot({native_disk}, kRestartSession), &clock);
      ASSERT_TRUE(restored);
      EXPECT_EQ(id, BindingFor(*restored, native_disk.native_key).logical_id);
      EXPECT_EQ(expected, ReadBookmark(store, id));
      EXPECT_EQ(0, store.PendingOutboxCount());
    }
  }
}

TEST_F(BookmarkSyncRecoveryTest,
       FirstAcknowledgementRaceKeepsPeerTitleAndNewLocalUrl) {
  const auto remote = RemotePage(99);
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(remote, "peer-new")}}));
  const auto planned = journal_.ReadProjection();
  ASSERT_TRUE(planned);
  auto native = AppliedPage(*planned, Key(99));
  const auto newer = PeerRename(ReadBookmark(store_, remote.id));
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(newer, "peer-newer")}}));
  const auto peer_state = ReadBookmark(store_, remote.id);
  // The first UI reply applied A, while the backend already holds B. A is
  // still an acknowledged target because its receipt belongs to this identity.
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(Snapshot({native})));
  native.url = "https://example.test/edited-after-first-apply";
  const auto merged_projection =
      journal_.ReconcileLocal(Snapshot({native}), &clock_);
  ASSERT_TRUE(merged_projection);
  const auto merged = ReadBookmark(store_, remote.id);
  EXPECT_EQ(peer_state.title, merged.title);
  EXPECT_EQ(native.url, merged.url);
  EXPECT_EQ(peer_state.field_versions.at("title"),
            merged.field_versions.at("title"));
  EXPECT_EQ(kLocalDevice, merged.field_versions.at("url").device_tiebreak);
  EXPECT_EQ(1, store_.PendingOutboxCount());
  ASSERT_TRUE(journal_.ReconcileLocal(Snapshot({native}), &clock_));
  EXPECT_EQ(merged, ReadBookmark(store_, remote.id));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncRecoveryTest,
       OldReceiptDoesNotReplayAnObservedEditButAllowsDeliberateUndo) {
  const auto seeded = journal_.ReconcileLocal(Snapshot({Page(1)}), &clock_);
  ASSERT_TRUE(seeded);
  auto native = AppliedPage(*seeded, Key(1));
  const auto id = BindingFor(*seeded, Key(1)).logical_id;
  const std::string first_url = native.url;
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(Snapshot({native})));
  AcknowledgeOutbox(store_);
  native.url = "https://example.test/local-u2";
  ASSERT_TRUE(journal_.ReconcileLocal(Snapshot({native}), &clock_));
  EXPECT_EQ(native.url, ReadBookmark(store_, id).url);
  EXPECT_EQ(1, store_.PendingOutboxCount());
  AcknowledgeOutbox(store_);
  const auto peer =
      PeerUrl(ReadBookmark(store_, id), "https://example.test/peer-u3");
  ASSERT_EQ(Result::kOk, store_.ApplyRemoteBatch(
                             {.changes = {ChangeFor(peer, "peer-url")}}));
  const auto expected = ReadBookmark(store_, id);
  // Native still has U2 and the original receipt. U2 was already consumed in
  // this observation session, so it must not be a new edit against peer U3.
  ASSERT_TRUE(journal_.ReconcileLocal(Snapshot({native}), &clock_));
  EXPECT_EQ(expected, ReadBookmark(store_, id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
  // An actual subsequent edit back to the receipt's U1 is distinguishable
  // from that repeated snapshot: compare with the last observed U2.
  native.url = first_url;
  ASSERT_TRUE(journal_.ReconcileLocal(Snapshot({native}), &clock_));
  const auto undone = ReadBookmark(store_, id);
  EXPECT_EQ(first_url, undone.url);
  EXPECT_EQ(kLocalDevice, undone.field_versions.at("url").device_tiebreak);
  EXPECT_NE(expected.field_versions.at("url"), undone.field_versions.at("url"));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncRecoveryTest,
       CopiedReceiptCannotClaimOrRebaseCloneIdentity) {
  const auto seeded = journal_.ReconcileLocal(Snapshot({Page(1)}), &clock_);
  ASSERT_TRUE(seeded);
  const auto original = AppliedPage(*seeded, Key(1));
  const auto original_id = BindingFor(*seeded, Key(1)).logical_id;
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(Snapshot({original})));
  const auto original_record = ReadBookmark(store_, original_id);
  AcknowledgeOutbox(store_);
  auto clone = original;
  clone.native_key = Key(2);
  clone.index = 1;
  clone.title = "Copied and edited";
  clone.explicitly_added = true;
  const auto projection =
      journal_.ReconcileLocal(Snapshot({original, clone}), &clock_);
  ASSERT_TRUE(projection);
  ASSERT_EQ(2u, projection->records.size());
  const auto clone_binding = BindingFor(*projection, clone.native_key);
  EXPECT_NE(original_id, clone_binding.logical_id);
  EXPECT_EQ(InitialBookmarkSyncId(clone.native_key), clone_binding.logical_id);
  EXPECT_EQ(original_record, ReadBookmark(store_, original_id));
  const auto clone_record = ReadBookmark(store_, clone_binding.logical_id);
  EXPECT_EQ(clone.title, clone_record.title);
  EXPECT_EQ(clone.url, clone_record.url);
  EXPECT_EQ(clone.created_at, clone_record.created_at);
  EXPECT_NE(original.apply_receipt, clone_binding.apply_receipt);
  const auto changes = ReadOutbox(store_);
  ASSERT_EQ(1u, changes.size());
  EXPECT_EQ(clone_binding.logical_id, changes[0].entity_id);
  // A known token for another record must not become this clone's baseline.
  EXPECT_FALSE(journal_.AcknowledgeNativeProjection(Snapshot({clone})));
  const auto applied_clone = AppliedPage(*projection, clone.native_key, 1);
  ASSERT_TRUE(journal_.AcknowledgeNativeProjection(
      Snapshot({original, applied_clone})));
  ASSERT_TRUE(
      journal_.ReconcileLocal(Snapshot({original, applied_clone}), &clock_));
  EXPECT_EQ(clone_record, ReadBookmark(store_, clone_binding.logical_id));
  EXPECT_EQ(1, store_.PendingOutboxCount());
}

TEST_F(BookmarkSyncRecoveryTest, PartialNativeApplyDoesNotEchoPeerEdits) {
  const auto seeded =
      journal_.ReconcileLocal(Snapshot({Page(1), Page(2, 1)}), &clock_);
  ASSERT_TRUE(seeded);
  const auto first_id = BindingFor(*seeded, Key(1)).logical_id;
  const auto second_id = BindingFor(*seeded, Key(2)).logical_id;
  const auto old_first = AppliedPage(*seeded, Key(1));
  const auto old_second = AppliedPage(*seeded, Key(2), 1);
  ASSERT_TRUE(
      journal_.AcknowledgeNativeProjection(Snapshot({old_first, old_second})));
  AcknowledgeOutbox(store_);
  const auto first_peer = PeerRename(ReadBookmark(store_, first_id));
  const auto second_peer = PeerRename(ReadBookmark(store_, second_id));
  ASSERT_EQ(Result::kOk,
            store_.ApplyRemoteBatch(
                {.changes = {ChangeFor(first_peer, "peer-first"),
                             ChangeFor(second_peer, "peer-second")}}));
  const auto expected_first = ReadBookmark(store_, first_id);
  const auto expected_second = ReadBookmark(store_, second_id);
  const auto planned = journal_.ReadProjection();
  ASSERT_TRUE(planned);
  const auto applied_first = AppliedPage(*planned, Key(1));
  // An interrupted projection left the second native node and its receipt at
  // A. Only the first node received B; there was no whole-projection ack.
  ASSERT_TRUE(
      journal_.ReconcileLocal(Snapshot({applied_first, old_second}), &clock_));
  EXPECT_EQ(expected_first, ReadBookmark(store_, first_id));
  EXPECT_EQ(expected_second, ReadBookmark(store_, second_id));
  EXPECT_EQ(0, store_.PendingOutboxCount());
}

TEST(BookmarkSyncRecoveryRestartTest,
     DeletedNativeSnapshotRequiresDeliberateRestoreAfterRestart) {
  for (const bool compact : {false, true}) {
    SCOPED_TRACE(compact);
    base::ScopedTempDir directory;
    ASSERT_TRUE(directory.CreateUniqueTempDir());
    const auto path = directory.GetPath().AppendASCII("sync.sqlite");
    NativeBookmarkEntry native_disk;
    base::Uuid deleted_id;
    BookmarkRecord old_live;
    BookmarkRecord tombstone;
    {
      SyncStore store;
      ASSERT_TRUE(store.Initialize(path));
      BookmarkSyncJournal journal(&store);
      HybridLogicalClock clock(kLocalDevice);
      const auto seeded = journal.ReconcileLocal(Snapshot({Page(1)}), &clock);
      ASSERT_TRUE(seeded);
      deleted_id = BindingFor(*seeded, Key(1)).logical_id;
      native_disk = AppliedPage(*seeded, Key(1));
      ASSERT_TRUE(journal.AcknowledgeNativeProjection(Snapshot({native_disk})));
      old_live = ReadBookmark(store, deleted_id);
      AcknowledgeOutbox(store);
      ASSERT_TRUE(journal.ReconcileLocal(
          Snapshot({}, kObservationSession, {native_disk.native_key}), &clock));
      tombstone = ReadBookmark(store, deleted_id);
      ASSERT_TRUE(tombstone.tombstone);
      AcknowledgeOutbox(store);
      if (compact) {
        ASSERT_EQ(Result::kOk,
                  store.CompactExpiredTombstones(
                      base::Time::Now() + base::Days(31), base::Days(30)));
      }
    }
    {
      SyncStore store;
      ASSERT_TRUE(store.Initialize(path));
      BookmarkSyncJournal journal(&store);
      HybridLogicalClock clock(kLocalDevice);
      ASSERT_FALSE(native_disk.explicitly_added);
      const auto stale = journal.ReconcileLocal(
          Snapshot({native_disk}, kRestartSession), &clock);
      ASSERT_TRUE(stale);
      EXPECT_TRUE(std::ranges::none_of(
          stale->records,
          [](const BookmarkRecord& record) { return !record.tombstone; }));
      EXPECT_EQ(0, store.PendingOutboxCount());
      SyncRecord old_record;
      ASSERT_EQ(
          compact ? Result::kNotFound : Result::kOk,
          store.GetRecord(EntityType::kBookmark, deleted_id, &old_record));
      if (!compact) {
        EXPECT_EQ(tombstone, std::get<BookmarkRecord>(old_record));
      }
      // Native Undo reuses its GUID and copied metadata. Only the explicit
      // live Add observation authorizes a new identity after durable deletion.
      auto undo = native_disk;
      undo.explicitly_added = true;
      const auto restored =
          journal.ReconcileLocal(Snapshot({undo}, kRestartSession), &clock);
      ASSERT_TRUE(restored);
      const auto restored_id =
          BindingFor(*restored, undo.native_key).logical_id;
      ASSERT_TRUE(restored_id.is_valid());
      EXPECT_NE(deleted_id, restored_id);
      const auto live = ReadBookmark(store, restored_id);
      EXPECT_FALSE(live.tombstone);
      EXPECT_EQ(undo.title, live.title);
      EXPECT_EQ(undo.url, live.url);
      EXPECT_EQ(undo.created_at, live.created_at);
      const auto changes = ReadOutbox(store);
      ASSERT_EQ(1u, changes.size());
      EXPECT_EQ(restored_id, changes[0].entity_id);
      EXPECT_EQ(ChangeKind::kUpsert, changes[0].kind);
      const auto repeated =
          journal.ReconcileLocal(Snapshot({undo}, kRestartSession), &clock);
      ASSERT_TRUE(repeated);
      EXPECT_EQ(restored_id, BindingFor(*repeated, undo.native_key).logical_id);
      EXPECT_EQ(live, ReadBookmark(store, restored_id));
      EXPECT_EQ(1, store.PendingOutboxCount());
      ASSERT_EQ(Result::kOk,
                store.ApplyRemoteBatch(
                    {.changes = {ChangeFor(old_live, "stale-peer")}}));
      ASSERT_EQ(
          compact ? Result::kNotFound : Result::kOk,
          store.GetRecord(EntityType::kBookmark, deleted_id, &old_record));
      if (!compact) {
        EXPECT_EQ(tombstone, std::get<BookmarkRecord>(old_record));
      }
      EXPECT_EQ(live, ReadBookmark(store, restored_id));
      EXPECT_EQ(1, store.PendingOutboxCount());
    }
  }
}

TEST(BookmarkSyncRecoveryRestartTest,
     ReceiptsAreIdempotentAndOldTargetsSurviveRestart) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  const auto remote = RemotePage(99);
  NativeBookmarkEntry native_a;
  NativeBookmarkEntry native_b;
  BookmarkRecord expected;
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                               {.changes = {ChangeFor(remote, "peer-new")}}));
    const auto first = journal.ReadProjection();
    ASSERT_TRUE(first);
    native_a = AppliedPage(*first, Key(99));
    const auto repeated = journal.ReadProjection();
    ASSERT_TRUE(repeated);
    EXPECT_EQ(native_a.apply_receipt,
              BindingFor(*repeated, Key(99)).apply_receipt);
    const auto newer = PeerRename(ReadBookmark(store, remote.id));
    ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                               {.changes = {ChangeFor(newer, "peer-newer")}}));
    expected = ReadBookmark(store, remote.id);
    const auto second = journal.ReadProjection();
    ASSERT_TRUE(second);
    native_b = AppliedPage(*second, Key(99));
    EXPECT_NE(native_a.apply_receipt, native_b.apply_receipt);
    const auto second_repeat = journal.ReadProjection();
    ASSERT_TRUE(second_repeat);
    EXPECT_EQ(native_b.apply_receipt,
              BindingFor(*second_repeat, Key(99)).apply_receipt);
    EXPECT_EQ(0, store.PendingOutboxCount());
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    const auto reopened = journal.ReadProjection();
    ASSERT_TRUE(reopened);
    EXPECT_EQ(native_b.apply_receipt,
              BindingFor(*reopened, Key(99)).apply_receipt);
    // The native file can still contain A after a crash. Its old receipt must
    // remain resolvable even after planning B and reopening the store.
    ASSERT_TRUE(journal.AcknowledgeNativeProjection(
        Snapshot({native_a}, kRestartSession)));
    ASSERT_TRUE(
        journal.ReconcileLocal(Snapshot({native_a}, kRestartSession), &clock));
    EXPECT_EQ(expected, ReadBookmark(store, remote.id));
    EXPECT_EQ(0, store.PendingOutboxCount());
    native_a.url = "https://example.test/local-after-restart";
    ASSERT_TRUE(
        journal.ReconcileLocal(Snapshot({native_a}, kRestartSession), &clock));
    const auto merged = ReadBookmark(store, remote.id);
    EXPECT_EQ(expected.title, merged.title);
    EXPECT_EQ(native_a.url, merged.url);
    EXPECT_EQ(expected.field_versions.at("title"),
              merged.field_versions.at("title"));
    EXPECT_EQ(1, store.PendingOutboxCount());
  }
}

TEST(BookmarkSyncRecoveryRestartTest,
     PersistedObservedEditIsNotReplayedWithTheOldReceipt) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("sync.sqlite");
  NativeBookmarkEntry native_disk;
  base::Uuid id;
  BookmarkRecord expected;
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    const auto seeded = journal.ReconcileLocal(Snapshot({Page(1)}), &clock);
    ASSERT_TRUE(seeded);
    id = BindingFor(*seeded, Key(1)).logical_id;
    native_disk = AppliedPage(*seeded, Key(1));
    ASSERT_TRUE(journal.AcknowledgeNativeProjection(Snapshot({native_disk})));
    AcknowledgeOutbox(store);
    native_disk.url = "https://example.test/saved-local-u2";
    ASSERT_TRUE(journal.ReconcileLocal(Snapshot({native_disk}), &clock));
    EXPECT_EQ(native_disk.url, ReadBookmark(store, id).url);
    AcknowledgeOutbox(store);
    const auto peer =
        PeerUrl(ReadBookmark(store, id), "https://example.test/peer-u3");
    ASSERT_EQ(Result::kOk, store.ApplyRemoteBatch(
                               {.changes = {ChangeFor(peer, "peer-url")}}));
    expected = ReadBookmark(store, id);
    // The native JSON saved U2 together with its unchanged old receipt. The
    // backend subsequently received U3, which has not reached that file yet.
  }
  {
    SyncStore store;
    ASSERT_TRUE(store.Initialize(path));
    BookmarkSyncJournal journal(&store);
    HybridLogicalClock clock(kLocalDevice);
    ASSERT_TRUE(journal.ReconcileLocal(Snapshot({native_disk}, kRestartSession),
                                       &clock));
    EXPECT_EQ(expected, ReadBookmark(store, id));
    EXPECT_EQ(0, store.PendingOutboxCount());
    ASSERT_TRUE(journal.ReconcileLocal(Snapshot({native_disk}, kRestartSession),
                                       &clock));
    EXPECT_EQ(expected, ReadBookmark(store, id));
    EXPECT_EQ(0, store.PendingOutboxCount());
  }
}

}  // namespace
}  // namespace ahoi::sync
