// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_history_writer.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
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
  history::URLRow previous_row;
  std::set<history::VisitID> previous_visit_ids;
  history::VisitSourceMap previous_sources;
};

bool SameURLRow(const history::URLRow& a, const history::URLRow& b) {
  return a.id() == b.id() && a.url() == b.url() && a.title() == b.title() &&
         a.visit_count() == b.visit_count() && a.typed_count() == b.typed_count() &&
         a.last_visit() == b.last_visit() && a.hidden() == b.hidden();
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
  std::vector<history::VisitID> removed_visits;
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
    history::VisitVector visits;
    if (!database->GetVisitsForURL(url_id, &visits)) {
      return false;
    }
    for (const auto& visit : visits) {
      if (!page.previous_visit_ids.contains(visit.visit_id) &&
          visit.visit_time == page.entry->last_visit) {
        // AddVisit can insert the visit then fail its visit_source INSERT.
        // The one serialized DBTask owns these new IDs even without an Arc
        // source row; no prior visit ID is ever removed.
        removed_visits.push_back(visit.visit_id);
        database->DeleteVisit(visit);
      }
    }
  }
  if (!created_urls.empty()) {
    // Deletes rows and visits and notifies observers, which were told about
    // the created rows by AddPagesWithDetails().
    backend->DeleteURLs(created_urls);
  }
  if (!backend->CommitForAhoiImport()) {
    return false;
  }
  if (!std::ranges::all_of(removed_visits, [database](history::VisitID id) {
        return database->GetVisitSource(id) == history::SOURCE_BROWSED;
      })) {
    return false;
  }
  return std::ranges::all_of(planned, [&](const PlannedPage& page) {
    history::URLRow row;
    const history::URLID url_id = database->GetRowForURL(page.entry->url, &row);
    if (!page.url_existed) {
      return url_id == 0;
    }
    if (!url_id || !SameURLRow(row, page.previous_row)) {
      return false;
    }
    history::VisitVector visits;
    history::VisitSourceMap sources;
    if (!database->GetVisitsForURL(url_id, &visits) ||
        !backend->GetVisitsSource(visits, &sources)) {
      return false;
    }
    std::set<history::VisitID> ids;
    for (const auto& visit : visits) {
      ids.insert(visit.visit_id);
    }
    return ids == page.previous_visit_ids && sources == page.previous_sources;
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
               !entry.url.has_username() && !entry.url.has_password() &&
               !entry.last_visit.is_null();
      })) {
    return outcome;
  }
  // Commit unrelated batched work first so the singleton transaction that
  // follows holds only this import. Without it the batch is not atomic.
  if (!backend->CommitForAhoiImport() ||
      !database->HasActiveTransactions()) {
    return outcome;
  }

  std::vector<PlannedPage> planned;
  history::URLRows rows;
  std::set<GURL> input_urls;
  for (const ArcHistoryEntry& entry : entries) {
    if (!input_urls.insert(entry.url).second) {
      return {};
    }
    if (backend->IsExpiredVisitTime(entry.last_visit)) {
      ++outcome.expired_pages;
      continue;
    }
    history::URLRow existing;
    const history::URLID url_id = database->GetRowForURL(entry.url, &existing);
    PlannedPage page{.entry = &entry,
                     .url_existed = url_id != 0,
                     .previous_row = existing};
    if (url_id) {
      history::VisitVector visits;
      if (!database->GetVisitsForURL(url_id, &visits) ||
          !backend->GetVisitsSource(visits, &page.previous_sources)) {
        return {};
      }
      if (std::ranges::any_of(visits, [&entry](const auto& visit) {
            return visit.visit_time == entry.last_visit;
          })) {
        ++outcome.deduplicated_pages;
        continue;
      }
      for (const auto& visit : visits) {
        page.previous_visit_ids.insert(visit.visit_id);
      }
    }
    planned.push_back(std::move(page));
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

  backend->AddPagesWithDetails(rows, hooks.omit_arc_source
                                         ? history::SOURCE_BROWSED
                                         : history::SOURCE_ARC_IMPORTED);
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
  if (hooks.fail_commit || !backend->CommitForAhoiImport()) {
    // The batch may already be durable; preserve Prepared for exact replay.
    outcome.status = ArcImportStatus::kRecoveryRequired;
    return outcome;
  }
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
