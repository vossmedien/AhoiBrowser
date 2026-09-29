// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_writer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/history/core/test/history_service_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

struct VisitSnapshot {
  base::Time time;
  history::VisitSource source = history::SOURCE_BROWSED;

  bool operator==(const VisitSnapshot&) const = default;
};

struct PageSnapshot {
  bool exists = false;
  std::u16string title;
  int visit_count = 0;
  int typed_count = 0;
  base::Time last_visit;
  bool hidden = false;
  std::vector<VisitSnapshot> visits;

  bool operator==(const PageSnapshot&) const = default;
};

// Reads one page's URL row and visits on the history sequence.
class InspectPageTask : public history::HistoryDBTask {
 public:
  InspectPageTask(GURL url, PageSnapshot* snapshot, base::OnceClosure done)
      : url_(std::move(url)), snapshot_(snapshot), done_(std::move(done)) {}

  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* database) override {
    history::URLRow row;
    const history::URLID url_id = database->GetRowForURL(url_, &row);
    snapshot_->exists = url_id != 0;
    if (!url_id) {
      return true;
    }
    snapshot_->title = row.title();
    snapshot_->visit_count = row.visit_count();
    snapshot_->typed_count = row.typed_count();
    snapshot_->last_visit = row.last_visit();
    snapshot_->hidden = row.hidden();
    history::VisitVector visits;
    database->GetVisitsForURL(url_id, &visits);
    history::VisitSourceMap sources;
    backend->GetVisitsSource(visits, &sources);
    for (const history::VisitRow& visit : visits) {
      const auto source = sources.find(visit.visit_id);
      snapshot_->visits.push_back({.time = visit.visit_time,
                                   .source = source == sources.end()
                                                 ? history::SOURCE_BROWSED
                                                 : source->second});
    }
    return true;
  }

  void DoneRunOnMainThread() override { std::move(done_).Run(); }

 private:
  const GURL url_;
  const raw_ptr<PageSnapshot> snapshot_;
  base::OnceClosure done_;
};

class ArcHistoryWriterTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(history_dir_.CreateUniqueTempDir());
    history_service_ =
        history::CreateHistoryService(history_dir_.GetPath(), true);
    ASSERT_TRUE(history_service_);
    now_ = base::Time::Now();
  }

  void TearDown() override {
    history_service_->Shutdown();
    history_service_.reset();
    task_environment_.RunUntilIdle();
  }

  ArcHistoryEntry Entry(const std::string& url,
                        int days_ago,
                        const std::u16string& title = u"Arc page") const {
    return {.url = GURL(url),
            .title = title,
            .visit_count = 4,
            .typed_count = 1,
            .last_visit = now_ - base::Days(days_ago)};
  }

  ArcHistoryWriteOutcome Write(std::vector<ArcHistoryEntry> entries,
                               ArcHistoryWriteTestHooks hooks = {}) {
    base::test::TestFuture<ArcHistoryWriteOutcome> outcome;
    ScheduleArcHistoryWrite(history_service_.get(), std::move(entries),
                            &tracker_, outcome.GetCallback(), hooks);
    return outcome.Get();
  }

  PageSnapshot Inspect(const std::string& url) {
    PageSnapshot snapshot;
    base::RunLoop run_loop;
    history_service_->ScheduleDBTask(
        FROM_HERE,
        std::make_unique<InspectPageTask>(GURL(url), &snapshot,
                                          run_loop.QuitClosure()),
        &tracker_);
    run_loop.Run();
    return snapshot;
  }

  void AddBrowsedVisit(const std::string& url, base::Time time) {
    history_service_->AddPage(GURL(url), time, history::SOURCE_BROWSED);
    history::BlockUntilHistoryProcessesPendingRequests(history_service_.get());
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir history_dir_;
  std::unique_ptr<history::HistoryService> history_service_;
  base::CancelableTaskTracker tracker_;
  base::Time now_;
};

TEST_F(ArcHistoryWriterTest, ImportAddsArcVisitsAndReplayIsANoOp) {
  const std::vector<ArcHistoryEntry> entries = {
      Entry("https://first.example/", 1, u"First"),
      Entry("https://second.example/", 2, u"Second")};

  const ArcHistoryWriteOutcome first = Write(entries);
  EXPECT_EQ(ArcImportStatus::kOk, first.status);
  EXPECT_EQ(2u, first.added_pages);
  EXPECT_EQ(2u, first.created_urls);
  EXPECT_EQ(0u, first.deduplicated_pages);

  const PageSnapshot page = Inspect("https://first.example/");
  ASSERT_TRUE(page.exists);
  EXPECT_EQ(u"First", page.title);
  EXPECT_EQ(4, page.visit_count);
  EXPECT_EQ(1, page.typed_count);
  EXPECT_EQ(entries[0].last_visit, page.last_visit);
  EXPECT_EQ(
      (std::vector<VisitSnapshot>{{.time = entries[0].last_visit,
                                   .source = history::SOURCE_ARC_IMPORTED}}),
      page.visits);

  // Replaying the same snapshot adds nothing and changes nothing.
  const ArcHistoryWriteOutcome replay = Write(entries);
  EXPECT_EQ(ArcImportStatus::kNoChanges, replay.status);
  EXPECT_EQ(0u, replay.added_pages);
  EXPECT_EQ(2u, replay.deduplicated_pages);
  EXPECT_EQ(page, Inspect("https://first.example/"));
}

TEST_F(ArcHistoryWriterTest, ExistingPageKeepsItsRowAndGainsOneArcVisit) {
  const base::Time browsed = now_ - base::Days(5);
  AddBrowsedVisit("https://known.example/", browsed);
  const PageSnapshot before = Inspect("https://known.example/");
  ASSERT_TRUE(before.exists);
  ASSERT_EQ(1u, before.visits.size());

  // One entry at a new time, one at exactly the browsed visit's time.
  const ArcHistoryEntry arc = Entry("https://known.example/", 1);
  ArcHistoryEntry same_time = arc;
  same_time.last_visit = browsed;

  const ArcHistoryWriteOutcome outcome = Write({arc});
  EXPECT_EQ(ArcImportStatus::kOk, outcome.status);
  EXPECT_EQ(1u, outcome.added_pages);
  EXPECT_EQ(0u, outcome.created_urls);

  const PageSnapshot after = Inspect("https://known.example/");
  EXPECT_EQ(before.title, after.title);
  EXPECT_EQ(before.visit_count, after.visit_count);
  EXPECT_EQ(before.typed_count, after.typed_count);
  ASSERT_EQ(2u, after.visits.size());
  EXPECT_EQ(before.visits[0], after.visits[0]);
  EXPECT_EQ((VisitSnapshot{.time = arc.last_visit,
                           .source = history::SOURCE_ARC_IMPORTED}),
            after.visits[1]);

  const ArcHistoryWriteOutcome duplicate = Write({same_time});
  EXPECT_EQ(ArcImportStatus::kNoChanges, duplicate.status);
  EXPECT_EQ(1u, duplicate.deduplicated_pages);
  EXPECT_EQ(after, Inspect("https://known.example/"));
}

TEST_F(ArcHistoryWriterTest, FailedWriteIsRolledBackExactly) {
  AddBrowsedVisit("https://known.example/", now_ - base::Days(5));
  const PageSnapshot known_before = Inspect("https://known.example/");
  ASSERT_TRUE(known_before.exists);

  const ArcHistoryWriteOutcome outcome =
      Write({Entry("https://known.example/", 1),
             Entry("https://created.example/", 2)},
            {.fail_after_write = true});
  EXPECT_EQ(ArcImportStatus::kTransactionFailed, outcome.status);
  EXPECT_EQ(0u, outcome.added_pages);

  EXPECT_EQ(known_before, Inspect("https://known.example/"));
  EXPECT_FALSE(Inspect("https://created.example/").exists);

  // The rolled-back pages import normally afterwards.
  EXPECT_EQ(ArcImportStatus::kOk,
            Write({Entry("https://created.example/", 2)}).status);
}

TEST_F(ArcHistoryWriterTest, SkipsExpiredPagesAndRejectsUnsafeInput) {
  const ArcHistoryWriteOutcome expired =
      Write({Entry("https://old.example/", 400)});
  EXPECT_EQ(ArcImportStatus::kNoChanges, expired.status);
  EXPECT_EQ(1u, expired.expired_pages);
  EXPECT_FALSE(Inspect("https://old.example/").exists);

  const ArcHistoryWriteOutcome unsafe =
      Write({Entry("https://fine.example/", 1),
             Entry("chrome-extension://abcdefghijklmnop/", 1)});
  EXPECT_EQ(ArcImportStatus::kTransactionFailed, unsafe.status);
  EXPECT_FALSE(Inspect("https://fine.example/").exists);
}

}  // namespace

}  // namespace ahoi::importer::arc
