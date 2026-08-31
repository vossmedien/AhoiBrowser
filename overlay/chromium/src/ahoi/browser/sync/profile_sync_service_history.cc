// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/sync/history_sync_filter.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "ahoi/browser/sync/sync_policy.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "net/base/network_interfaces.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ahoi::sync {

void ProfileSyncService::ApplyRemoteHistory(
    const std::vector<HistoryRecord>& records) {
  if (!sync_enabled_ || backend_.is_null() || !history_service_) {
    return;
  }
  for (const HistoryRecord& record : records) {
    auto applied = applied_history_versions_.find(record.id);
    if (applied != applied_history_versions_.end() &&
        applied->second >= record.version) {
      continue;
    }
    applied_history_versions_[record.id] = record.version;
    if (record.version.stamp.device_tiebreak ==
        local_device_id_.AsLowercaseString()) {
      continue;
    }
    const GURL url(record.url);
    if (!IsSafeHistoryUrlForSync(url)) {
      continue;
    }
    if (!record.tombstone) {
      history_service_->AddPageWithDetails(url, base::UTF8ToUTF16(record.title),
                                           1, 0, record.last_visit, false,
                                           history::SOURCE_SYNCED);
      continue;
    }
    ++pending_remote_history_deletions_;
    history_service_->ExpireHistoryBetween(
        {url}, std::nullopt, record.last_visit,
        record.last_visit + base::Microseconds(1), false,
        base::BindOnce(&ProfileSyncService::OnRemoteHistoryExpired,
                       weak_ptr_factory_.GetWeakPtr()),
        &history_task_tracker_);
  }
}

void ProfileSyncService::OnRemoteHistoryExpired() {
  if (pending_remote_history_deletions_ > 0) {
    --pending_remote_history_deletions_;
  }
}

void ProfileSyncService::OnURLVisited(history::HistoryService* history_service,
                                      const history::VisitedURLInfo& info) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      history_service != history_service_ ||
      !ShouldSyncHistoryVisit({
          .url = info.url_row.url(),
          .hidden = info.url_row.hidden(),
          .response_is_404 = info.response_code_category ==
                             history::VisitResponseCodeCategory::k404,
          .source_is_browsed =
              !info.visit_row.source ||
              *info.visit_row.source == history::SOURCE_BROWSED,
      })) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::AddHistoryVisit)
      .WithArgs(info.url_row.url().spec(),
                base::UTF16ToUTF8(info.url_row.title()),
                info.visit_row.visit_time,
                std::string(ui::PageTransitionGetCoreTransitionString(
                    info.visit_row.transition)),
                info.visit_row.visit_id)
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> service,
             std::optional<SyncStateSnapshot> state) {
            if (!service) {
              return;
            }
            service->OnBackendState(std::move(state));
            service->SyncNow();
          },
          backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& info) {
  if (shutting_down_ || !sync_enabled_ || backend_.is_null() ||
      history_service != history_service_ ||
      pending_remote_history_deletions_ > 0 || info.is_from_expiration() ||
      info.deletion_reason() ==
          history::DeletionInfo::Reason::kDeleteAllForeignVisits) {
    return;
  }
  std::set<std::string> urls;
  if (!info.IsAllHistory()) {
    for (const history::URLRow& row : info.deleted_rows()) {
      if (IsSafeHistoryUrlForSync(row.url())) {
        urls.insert(row.url().spec());
      }
    }
    if (info.restrict_urls()) {
      for (const GURL& url : *info.restrict_urls()) {
        if (IsSafeHistoryUrlForSync(url)) {
          urls.insert(url.spec());
        }
      }
    }
  }
  const bool valid_range = info.time_range().IsValid();
  if (!info.IsAllHistory() && urls.empty() && !valid_range) {
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::TombstoneHistory)
      .WithArgs(std::vector<std::string>(urls.begin(), urls.end()),
                valid_range ? info.time_range().begin() : base::Time(),
                valid_range ? info.time_range().end() : base::Time(),
                info.IsAllHistory())
      .Then(base::BindOnce(
          [](base::WeakPtr<ProfileSyncService> service,
             std::optional<SyncStateSnapshot> state) {
            if (!service) {
              return;
            }
            service->OnBackendState(std::move(state));
            service->SyncNow();
          },
          backend_weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  if (history_service == history_service_) {
    history_service_ = nullptr;
  }
}

}  // namespace ahoi::sync
