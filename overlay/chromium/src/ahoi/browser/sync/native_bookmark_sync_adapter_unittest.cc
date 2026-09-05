// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"

#include <memory>
#include <utility>

#include "ahoi/browser/sync/bookmark_sync_journal.h"
#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_store.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::sync {
namespace {

base::Uuid Id(const char* text) {
  return base::Uuid::ParseLowercase(text);
}
constexpr char kFolder[] = "b1000000-0000-4000-8000-000000000001";
constexpr char kPage[] = "b1000000-0000-4000-8000-000000000002";
constexpr char kDevice[] = "b1000000-0000-4000-8000-0000000000a0";

BookmarkSyncProjection RemoteFolder() {
  const auto time = base::Time::UnixEpoch() + base::Seconds(1);
  BookmarkRecord folder{
      .id = Id(kFolder),
      .root_kind = BookmarkRoot::kBookmarkBar,
      .sort_key = "M",
      .title = "Folder",
      .created_at = time,
      .version = {
          .stamp = {.physical_time_us =
                        time.ToDeltaSinceWindowsEpoch().InMicroseconds(),
                    .device_tiebreak = kDevice}}};
  BookmarkRecord page = folder;
  page.id = Id(kPage);
  page.kind = BookmarkKind::kUrl;
  page.root_kind.reset();
  page.parent_id = folder.id;
  page.title = "Ahoi";
  page.url = "https://example.test/page";
  return {{folder, page},
          {{folder.id, NativeBookmarkKey(folder.id, false)},
           {page.id, NativeBookmarkKey(page.id, false)}}};
}

class NativeBookmarkSyncAdapterTest : public testing::Test {
 protected:
  void SetUp() override {
    model_ = std::make_unique<bookmarks::BookmarkModel>(
        std::make_unique<bookmarks::TestBookmarkClient>());
    service_ =
        std::make_unique<BookmarkMergedSurfaceService>(model_.get(), nullptr);
    model_->LoadEmptyForTest();
    service_->LoadForTesting({});
    adapter_ = std::make_unique<NativeBookmarkSyncAdapter>(
        service_.get(),
        base::BindRepeating(&NativeBookmarkSyncAdapterTest::OnSnapshot,
                            base::Unretained(this)));
    Drain();
    ASSERT_TRUE(adapter_->ready());
  }
  void OnSnapshot(uint64_t generation, NativeBookmarkSnapshot snapshot) {
    generation_ = generation;
    snapshot_ = std::move(snapshot);
    ++callbacks_;
  }
  void Drain() { base::RunLoop().RunUntilIdle(); }

  base::test::TaskEnvironment environment_;
  std::unique_ptr<bookmarks::BookmarkModel> model_;
  std::unique_ptr<BookmarkMergedSurfaceService> service_;
  std::unique_ptr<NativeBookmarkSyncAdapter> adapter_;
  NativeBookmarkSnapshot snapshot_;
  uint64_t generation_ = 0;
  int callbacks_ = 0;
};

TEST_F(NativeBookmarkSyncAdapterTest, CapturesNativeRootsAndNestedIdentity) {
  const auto* folder =
      model_->AddFolder(model_->bookmark_bar_node(), 0, u"Ordner");
  const auto* page =
      model_->AddURL(folder, 0, u"Seite", GURL("https://example.test"));
  Drain();
  ASSERT_EQ(2u, snapshot_.entries.size());
  EXPECT_EQ(BookmarkRoot::kBookmarkBar, snapshot_.entries[0].root);
  EXPECT_FALSE(snapshot_.entries[0].parent_key);
  EXPECT_EQ(NativeBookmarkKey(page->uuid(), false),
            snapshot_.entries[1].native_key);
  EXPECT_EQ(NativeBookmarkKey(folder->uuid(), false),
            snapshot_.entries[1].parent_key);
  EXPECT_FALSE(snapshot_.entries[1].root);
}

TEST_F(NativeBookmarkSyncAdapterTest, AppliesRealModelWithoutNavigationOrEcho) {
  const auto projection = RemoteFolder();
  const int before = callbacks_;
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  Drain();
  ASSERT_EQ(1u, model_->bookmark_bar_node()->children().size());
  const auto* folder = model_->bookmark_bar_node()->children()[0].get();
  ASSERT_EQ(1u, folder->children().size());
  EXPECT_EQ(Id(kFolder), folder->uuid());
  EXPECT_EQ(Id(kPage), folder->children()[0]->uuid());
  EXPECT_EQ(GURL("https://example.test/page"), folder->children()[0]->url());
  EXPECT_EQ(before, callbacks_);
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  EXPECT_EQ(folder, model_->bookmark_bar_node()->children()[0].get());
  EXPECT_EQ(1u, folder->children().size());
}

TEST_F(NativeBookmarkSyncAdapterTest, PendingLocalEditRejectsOldRemoteReply) {
  auto projection = RemoteFolder();
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  const auto* folder = model_->bookmark_bar_node()->children()[0].get();
  const uint64_t old_generation = generation_;
  model_->SetTitle(folder, u"Local",
                   bookmarks::metrics::BookmarkEditSource::kOther);
  EXPECT_FALSE(adapter_->ApplyProjection(projection, old_generation));
  Drain();
  EXPECT_FALSE(adapter_->ApplyProjection(projection, old_generation));
  EXPECT_EQ(u"Local", folder->GetTitle());
}

TEST_F(NativeBookmarkSyncAdapterTest,
       MissingParentNeverCreatesPlaceholderFolder) {
  auto projection = RemoteFolder();
  projection.records.erase(projection.records.begin());
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  EXPECT_TRUE(model_->bookmark_bar_node()->children().empty());
}

TEST_F(NativeBookmarkSyncAdapterTest, NativeFolderRemovalCapturesDescendants) {
  const auto* folder =
      model_->AddFolder(model_->bookmark_bar_node(), 0, u"Folder");
  const auto* page =
      model_->AddURL(folder, 0, u"Child", GURL("https://example.test"));
  const std::string folder_key = NativeBookmarkKey(folder->uuid(), false);
  const std::string page_key = NativeBookmarkKey(page->uuid(), false);
  Drain();
  model_->Remove(folder, bookmarks::metrics::BookmarkEditSource::kOther,
                 FROM_HERE);
  Drain();
  ASSERT_EQ(2u, snapshot_.removed_keys.size());
  EXPECT_EQ(folder_key, snapshot_.removed_keys[0]);
  EXPECT_EQ(page_key, snapshot_.removed_keys[1]);
  EXPECT_TRUE(snapshot_.entries.empty());
}

TEST_F(NativeBookmarkSyncAdapterTest,
       AccountRootTeardownDoesNotExportDeletion) {
  model_->CreateAccountPermanentFolders();
  model_->AddURL(model_->account_bookmark_bar_node(), 0, u"Account",
                 GURL("https://example.test/account"));
  Drain();
  ASSERT_EQ(1u, snapshot_.entries.size());
  EXPECT_TRUE(snapshot_.entries[0].native_key.starts_with("account:"));
  model_->RemoveAccountPermanentFolders();
  Drain();
  EXPECT_TRUE(snapshot_.entries.empty());
  EXPECT_TRUE(snapshot_.removed_keys.empty());
}

TEST_F(NativeBookmarkSyncAdapterTest,
       ObservedStorageMoveRetainsItsPreviousKey) {
  model_->CreateAccountPermanentFolders();
  const auto* page = model_->AddURL(model_->bookmark_bar_node(), 0, u"Move",
                                    GURL("https://example.test/move"));
  const std::string old_key = NativeBookmarkKey(page->uuid(), false);
  Drain();
  adapter_->AcknowledgeCapture(generation_);
  model_->Move(page, model_->account_bookmark_bar_node(), 0);
  Drain();
  ASSERT_EQ(1u, snapshot_.entries.size());
  EXPECT_EQ(old_key, snapshot_.entries[0].previous_native_key);
  EXPECT_EQ(NativeBookmarkKey(page->uuid(), true),
            snapshot_.entries[0].native_key);
  EXPECT_TRUE(snapshot_.removed_keys.empty());
}

TEST_F(NativeBookmarkSyncAdapterTest, TombstonesRemoveOnlyOwnedNativeNodes) {
  auto projection = RemoteFolder();
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  model_->AddURL(model_->other_node(), 0, u"Unrelated",
                 GURL("https://example.test/unrelated"));
  Drain();
  for (auto& record : projection.records) {
    record.tombstone = true;
  }
  ASSERT_TRUE(adapter_->ApplyProjection(projection, generation_));
  EXPECT_TRUE(model_->bookmark_bar_node()->children().empty());
  ASSERT_EQ(1u, model_->other_node()->children().size());
  EXPECT_EQ(u"Unrelated", model_->other_node()->children()[0]->GetTitle());
}

TEST_F(NativeBookmarkSyncAdapterTest,
       RealNativeRoundTripHasNoRepeatOutboxWrite) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  HybridLogicalClock clock(kDevice);
  BookmarkSyncJournal journal(&store);
  model_->AddURL(model_->bookmark_bar_node(), 0, u"Native",
                 GURL("https://example.test/native"));
  Drain();
  auto projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1, store.PendingOutboxCount());
  ASSERT_TRUE(adapter_->ApplyProjection(*projection, generation_));
  ASSERT_TRUE(journal.AcknowledgeNativeProjection(adapter_->Capture()));
  adapter_->AcknowledgeCapture(generation_);
  ASSERT_TRUE(journal.ReconcileLocal(adapter_->Capture(), &clock));
  EXPECT_EQ(1, store.PendingOutboxCount());
  EXPECT_EQ(1u, model_->bookmark_bar_node()->children().size());
}

TEST_F(NativeBookmarkSyncAdapterTest, DestructionCancelsPendingObserverWork) {
  const int before = callbacks_;
  model_->AddFolder(model_->bookmark_bar_node(), 0, u"Pending");
  adapter_.reset();
  Drain();
  EXPECT_EQ(before, callbacks_);
}

TEST_F(NativeBookmarkSyncAdapterTest, MoveThenRemoveKeepsTheKnownDeletionKey) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  HybridLogicalClock clock(kDevice);
  BookmarkSyncJournal journal(&store);
  model_->CreateAccountPermanentFolders();
  const auto* page = model_->AddURL(model_->bookmark_bar_node(), 0, u"Move",
                                    GURL("https://example.test/move-delete"));
  Drain();
  auto projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->records.size());
  const base::Uuid logical = projection->records[0].id;
  ASSERT_TRUE(adapter_->ApplyProjection(*projection, generation_));
  ASSERT_TRUE(journal.AcknowledgeNativeProjection(adapter_->Capture()));
  adapter_->AcknowledgeCapture(generation_);
  model_->Move(page, model_->account_bookmark_bar_node(), 0);
  model_->Remove(page, bookmarks::metrics::BookmarkEditSource::kOther,
                 FROM_HERE);
  Drain();
  projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->records.size());
  EXPECT_EQ(logical, projection->records[0].id);
  EXPECT_TRUE(projection->records[0].tombstone);
  ASSERT_TRUE(adapter_->ApplyProjection(*projection, generation_));
  EXPECT_TRUE(model_->bookmark_bar_node()->children().empty());
  EXPECT_TRUE(model_->account_bookmark_bar_node()->children().empty());
}

TEST_F(NativeBookmarkSyncAdapterTest,
       CredentialMetadataBlocksReconciliationWithoutExportOrSanitizing) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  HybridLogicalClock clock(kDevice);
  BookmarkSyncJournal journal(&store);
  const GURL private_url("https://fixture-user:fixture-secret@example.test/");
  const auto* page =
      model_->AddURL(model_->bookmark_bar_node(), 0, u"Local", private_url);
  model_->AddURL(model_->other_node(), 0, u"Valid",
                 GURL("https://example.test/"));
  Drain();
  ASSERT_TRUE(snapshot_.local_data_blocked);
  EXPECT_TRUE(adapter_->local_data_blocked());
  ASSERT_EQ(1u, snapshot_.entries.size());
  EXPECT_EQ("https://example.test/", snapshot_.entries[0].url);
  EXPECT_FALSE(journal.ReconcileLocal(snapshot_, &clock));
  EXPECT_FALSE(journal.AcknowledgeNativeProjection(snapshot_));
  EXPECT_EQ(0, store.PendingOutboxCount());
  EXPECT_EQ(private_url, page->url());
  EXPECT_FALSE(adapter_->ApplyProjection(RemoteFolder(), generation_));
  EXPECT_EQ(private_url, page->url());

  model_->SetURL(page, GURL("https://example.test/corrected"),
                 bookmarks::metrics::BookmarkEditSource::kOther);
  Drain();
  EXPECT_FALSE(snapshot_.local_data_blocked);
  auto projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  EXPECT_EQ(2u, projection->records.size());
  EXPECT_TRUE(adapter_->ApplyProjection(*projection, generation_));
}

TEST_F(NativeBookmarkSyncAdapterTest,
       InvalidNativeEditCannotBeOverwrittenByEarlierSyncedValue) {
  SyncStore store;
  ASSERT_TRUE(store.InitializeInMemory());
  HybridLogicalClock clock(kDevice);
  BookmarkSyncJournal journal(&store);
  const auto* page = model_->AddURL(model_->bookmark_bar_node(), 0, u"Page",
                                    GURL("https://example.test/original"));
  Drain();
  auto projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  const base::Uuid logical = projection->records[0].id;
  ASSERT_TRUE(adapter_->ApplyProjection(*projection, generation_));
  ASSERT_TRUE(journal.AcknowledgeNativeProjection(adapter_->Capture()));
  adapter_->AcknowledgeCapture(generation_);

  const GURL private_url("https://fixture-user:fixture-secret@example.test/");
  model_->SetURL(page, private_url,
                 bookmarks::metrics::BookmarkEditSource::kOther);
  Drain();
  ASSERT_TRUE(snapshot_.local_data_blocked);
  EXPECT_FALSE(adapter_->ApplyProjection(*projection, generation_));
  EXPECT_EQ(private_url, page->url());

  // Clear is an explicit removal, even if the latest content could not sync.
  model_->RemoveAllUserBookmarks(FROM_HERE);
  Drain();
  EXPECT_FALSE(snapshot_.local_data_blocked);
  projection = journal.ReconcileLocal(snapshot_, &clock);
  ASSERT_TRUE(projection);
  ASSERT_EQ(1u, projection->records.size());
  EXPECT_EQ(logical, projection->records[0].id);
  EXPECT_TRUE(projection->records[0].tombstone);
}

TEST_F(NativeBookmarkSyncAdapterTest,
       NonWebMetadataDoesNotTriggerPrivacyPause) {
  model_->AddURL(model_->bookmark_bar_node(), 0, u"Native",
                 GURL("chrome://bookmarks/"));
  model_->AddURL(model_->other_node(), 0, u"Local file",
                 GURL("file:///tmp/bookmark-fixture.html"));
  Drain();
  ASSERT_EQ(2u, snapshot_.entries.size());
  EXPECT_FALSE(snapshot_.local_data_blocked);
  EXPECT_FALSE(adapter_->local_data_blocked());
}

}  // namespace
}  // namespace ahoi::sync
