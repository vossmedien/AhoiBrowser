// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_writer.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/location.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

struct PlannedPage {
  const ArcHistoryEntry* entry = nullptr;
  bool url_existed = false;
};

bool HasVisitAt(history::HistoryDatabase* database,
                history::URLID url_id,
                base::Time time) {
  history::VisitVector visits;
  return database->GetVisitsForURL(url_id, &visits) &&
         std::ranges::any_of(visits, [time](const history::VisitRow& visit) {
           return visit.visit_time == time;
         });
}

// The visit this importer added for a page: exact time and Arc source. The
// dedup check guarantees no such visit existed before the batch.
std::optional<history::VisitRow> FindImportedVisit(
    history::HistoryBackend* backend,
    history::HistoryDatabase* database,
    history::URLID url_id,
    base::Time time) {
  history::VisitVector visits;
  if (!database->GetVisitsForURL(url_id, &visits)) {
    return std::nullopt;
  }
  std::erase_if(visits, [time](const history::VisitRow& visit) {
    return visit.visit_time != time;
  });
  history::VisitSourceMap sources;
  if (visits.empty() || !backend->GetVisitsSource(visits, &sources)) {
    return std::nullopt;
  }
  for (const history::VisitRow& visit : visits) {
    const auto source = sources.find(visit.visit_id);
    if (source != sources.end() &&
        source->second == history::SOURCE_ARC_IMPORTED) {
      return visit;
    }
  }
  return std::nullopt;
}

bool IsWritten(history::HistoryBackend* backend,
               history::HistoryDatabase* database,
               const PlannedPage& page) {
  history::URLRow row;
  const history::URLID url_id = database->GetRowForURL(page.entry->url, &row);
  return url_id &&
         FindImportedVisit(backend, database, url_id, page.entry->last_visit)
             .has_value();
}

// Removes exactly what AddPagesWithDetails() added: a created URL row with
// its single visit, or one Arc visit on a URL row it left untouched.
bool RollBack(history::HistoryBackend* backend,
              history::HistoryDatabase* database,
              const std::vector<PlannedPage>& planned) {
  std::vector<GURL> created_urls;
  for (const PlannedPage& page : planned) {
    history::URLRow row;
    const history::URLID url_id = database->GetRowForURL(page.entry->url, &row);
    if (!url_id) {
      continue;
    }
    if (!page.url_existed) {
      created_urls.push_back(page.entry->url);
      continue;
    }
    // AddPagesWithDetails() added a visit without touching the URL row or
    // notifying observers, so deleting that row is the exact inverse.
    if (const std::optional<history::VisitRow> visit = FindImportedVisit(
            backend, database, url_id, page.entry->last_visit)) {
      database->DeleteVisit(*visit);
    }
  }
  if (!created_urls.empty()) {
    // Deletes rows and visits and notifies observers, which were told about
    // the created rows by AddPagesWithDetails().
    backend->DeleteURLs(created_urls);
  }
  backend->CommitForAhoiImport();
  return std::ranges::all_of(planned, [&](const PlannedPage& page) {
    history::URLRow row;
    const history::URLID url_id = database->GetRowForURL(page.entry->url, &row);
    if (!page.url_existed) {
      return url_id == 0;
    }
    return url_id != 0 &&
           !FindImportedVisit(backend, database, url_id, page.entry->last_visit)
                .has_value();
  });
}

class ArcHistoryWriteTask : public history::HistoryDBTask {
 public:
  ArcHistoryWriteTask(std::vector<ArcHistoryEntry> entries,
                      ArcHistoryWriteTestHooks hooks,
                      ArcHistoryWriteCallback callback)
      : entries_(std::move(entries)),
        hooks_(hooks),
        callback_(std::move(callback)) {}
  ArcHistoryWriteTask(const ArcHistoryWriteTask&) = delete;
  ArcHistoryWriteTask& operator=(const ArcHistoryWriteTask&) = delete;
  ~ArcHistoryWriteTask() override = default;

  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* database) override {
    outcome_ = ApplyArcHistoryEntries(backend, database, entries_, hooks_);
    entries_.clear();
    entries_.shrink_to_fit();
    return true;
  }

  void DoneRunOnMainThread() override {
    if (callback_) {
      std::move(callback_).Run(outcome_);
    }
  }

 private:
  std::vector<ArcHistoryEntry> entries_;
  const ArcHistoryWriteTestHooks hooks_;
  ArcHistoryWriteCallback callback_;
  ArcHistoryWriteOutcome outcome_;
};

}  // namespace

ArcHistoryWriteOutcome ApplyArcHistoryEntries(
    history::HistoryBackend* backend,
    history::HistoryDatabase* database,
    const std::vector<ArcHistoryEntry>& entries,
    const ArcHistoryWriteTestHooks& hooks) {
  ArcHistoryWriteOutcome outcome;
  if (!backend || !database || entries.size() > kMaxArcHistoryRows ||
      !std::ranges::all_of(entries, [](const ArcHistoryEntry& entry) {
        return entry.url.is_valid() && entry.url.SchemeIsHTTPOrHTTPS() &&
               !entry.last_visit.is_null();
      })) {
    return outcome;
  }
  // Commit unrelated batched work first so the singleton transaction that
  // follows holds only this import. Without it the batch is not atomic.
  backend->CommitForAhoiImport();
  if (database->transaction_nesting() != 1) {
    return outcome;
  }

  std::vector<PlannedPage> planned;
  history::URLRows rows;
  for (const ArcHistoryEntry& entry : entries) {
    if (backend->IsExpiredVisitTime(entry.last_visit)) {
      ++outcome.expired_pages;
      continue;
    }
    history::URLRow existing;
    const history::URLID url_id = database->GetRowForURL(entry.url, &existing);
    if (url_id && HasVisitAt(database, url_id, entry.last_visit)) {
      ++outcome.deduplicated_pages;
      continue;
    }
    planned.push_back({.entry = &entry, .url_existed = url_id != 0});
    history::URLRow row(entry.url);
    row.set_title(entry.title);
    row.set_visit_count(std::max(1, entry.visit_count));
    row.set_typed_count(std::max(0, entry.typed_count));
    row.set_last_visit(entry.last_visit);
    row.set_hidden(false);
    rows.push_back(std::move(row));
  }
  if (rows.empty()) {
    outcome.status = ArcImportStatus::kNoChanges;
    return outcome;
  }

  backend->AddPagesWithDetails(rows, history::SOURCE_ARC_IMPORTED);
  const bool written =
      !hooks.fail_after_write &&
      std::ranges::all_of(planned, [&](const PlannedPage& page) {
        return IsWritten(backend, database, page);
      });
  if (!written) {
    outcome.status = RollBack(backend, database, planned)
                         ? ArcImportStatus::kTransactionFailed
                         : ArcImportStatus::kRecoveryRequired;
    outcome.expired_pages = 0;
    outcome.deduplicated_pages = 0;
    return outcome;
  }
  backend->CommitForAhoiImport();
  outcome.status = ArcImportStatus::kOk;
  outcome.added_pages = planned.size();
  outcome.created_urls = static_cast<size_t>(std::ranges::count_if(
      planned, [](const PlannedPage& page) { return !page.url_existed; }));
  return outcome;
}

void ScheduleArcHistoryWrite(history::HistoryService* history_service,
                             std::vector<ArcHistoryEntry> entries,
                             base::CancelableTaskTracker* tracker,
                             ArcHistoryWriteCallback callback,
                             ArcHistoryWriteTestHooks hooks) {
  if (!history_service || !tracker || !callback) {
    if (callback) {
      std::move(callback).Run({});
    }
    return;
  }
  history_service->ScheduleDBTask(
      FROM_HERE,
      std::make_unique<ArcHistoryWriteTask>(std::move(entries), hooks,
                                            std::move(callback)),
      tracker);
}

}  // namespace ahoi::importer::arc
