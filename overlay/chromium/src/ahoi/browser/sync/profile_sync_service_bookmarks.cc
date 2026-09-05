// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/sync/native_bookmark_sync_adapter.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/profile_sync_prefs.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/functional/bind.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"

namespace ahoi::sync {

bool ProfileSyncService::bookmark_sync_enabled() const {
  return profile_ && !shutting_down_ && profile_->IsRegularProfile() &&
         !profile_->IsOffTheRecord() &&
         profile_->GetPrefs()->GetBoolean(kBookmarkSyncEnabledPref);
}

bool ProfileSyncService::SetBookmarkSyncEnabled(bool enabled) {
  if (!profile_ || shutting_down_ || !profile_->IsRegularProfile() ||
      profile_->IsOffTheRecord()) {
    return false;
  }
  profile_->GetPrefs()->SetBoolean(kBookmarkSyncEnabledPref, enabled);
  return true;
}

base::CallbackListSubscription ProfileSyncService::ObserveBookmarkSync(
    base::RepeatingClosure callback) {
  return bookmark_status_callbacks_.Add(std::move(callback));
}

void ProfileSyncService::SetBookmarkSyncIssue(BookmarkSyncIssue issue) {
  if (bookmark_sync_issue_ != issue) {
    bookmark_sync_issue_ = issue;
    bookmark_status_callbacks_.Notify();
  }
}

void ProfileSyncService::InitializeBookmarkSync() {
  sync_pref_registrar_.Add(
      kBookmarkSyncEnabledPref,
      base::BindRepeating(&ProfileSyncService::OnBookmarkSyncPrefChanged,
                          weak_ptr_factory_.GetWeakPtr()));
}

void ProfileSyncService::StopBookmarkSync() {
  bookmark_weak_ptr_factory_.InvalidateWeakPtrs();
  bookmark_adapter_.reset();
  bookmarks_seeded_ = false;
  SetBookmarkSyncIssue(BookmarkSyncIssue::kNone);
}

void ProfileSyncService::OnBookmarkSyncPrefChanged() {
  StopBookmarkSync();
  if (!profile_ || shutting_down_) {
    return;
  }
  if (!backend_.is_null()) {
    backend_.AsyncCall(&ProfileSyncBackend::SetBookmarkSyncEnabled)
        .WithArgs(bookmark_sync_enabled())
        .Then(base::BindOnce(&ProfileSyncService::OnBackendState,
                             backend_weak_ptr_factory_.GetWeakPtr()));
  }
  NotifyObservers();
}

void ProfileSyncService::RefreshBookmarkProjection() {
  if (!bookmark_sync_enabled() || !sync_enabled_ || !backend_ready_ ||
      backend_.is_null()) {
    return;
  }
  if (!bookmark_adapter_) {
    auto* service =
        BookmarkMergedSurfaceServiceFactory::GetForProfile(profile_);
    if (!service) {
      return;
    }
    bookmark_adapter_ = std::make_unique<NativeBookmarkSyncAdapter>(
        service,
        base::BindRepeating(&ProfileSyncService::OnNativeBookmarkSnapshot,
                            bookmark_weak_ptr_factory_.GetWeakPtr()));
    return;  // The initial capture merges native data before any remote apply.
  }
  if (!bookmarks_seeded_) {
    if (bookmark_sync_issue_ != BookmarkSyncIssue::kNone) {
      bookmark_adapter_->RequestCapture();
    }
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::ReadBookmarkProjection)
      .Then(base::BindOnce(&ProfileSyncService::OnBookmarkProjection,
                           bookmark_weak_ptr_factory_.GetWeakPtr(),
                           bookmark_adapter_->generation(), false));
}

void ProfileSyncService::OnNativeBookmarkSnapshot(
    uint64_t generation,
    NativeBookmarkSnapshot snapshot) {
  if (!bookmark_adapter_ || !bookmark_sync_enabled() || !sync_enabled_ ||
      backend_.is_null()) {
    return;
  }
  // A failed local journal write must not be followed by a stale remote apply
  // that would erase the still-uncommitted native edit.
  bookmarks_seeded_ = false;
  if (snapshot.local_data_blocked) {
    SetBookmarkSyncIssue(BookmarkSyncIssue::kUnsupportedLocalData);
    return;
  }
  backend_.AsyncCall(&ProfileSyncBackend::MergeLocalBookmarks)
      .WithArgs(std::move(snapshot))
      .Then(base::BindOnce(&ProfileSyncService::OnBookmarkProjection,
                           bookmark_weak_ptr_factory_.GetWeakPtr(), generation,
                           true));
}

void ProfileSyncService::OnBookmarkProjection(
    uint64_t generation,
    bool local_change,
    std::optional<BookmarkSyncProjection> projection) {
  if (!bookmark_adapter_ || !bookmark_sync_enabled() || !sync_enabled_ ||
      backend_.is_null() || generation != bookmark_adapter_->generation() ||
      (!local_change && !bookmarks_seeded_)) {
    return;
  }
  if (projection &&
      (!projection->authorization || !projection->authorization.Run())) {
    // A provider account/consent event may precede OnBackendState. Even while
    // prefs and this facade still look approved, an old backend reply must
    // not start a native apply. Reapproval never revives this original scope.
    bookmarks_seeded_ = false;
    SetBookmarkSyncIssue(BookmarkSyncIssue::kAuthorizationChanged);
    return;
  }
  if (!projection ||
      !bookmark_adapter_->ApplyProjection(*projection, generation)) {
    SetBookmarkSyncIssue(bookmark_adapter_->local_data_blocked()
                             ? BookmarkSyncIssue::kUnsupportedLocalData
                             : BookmarkSyncIssue::kReconciliationFailed);
    return;
  }
  if (local_change) {
    bookmarks_seeded_ = true;
  }
  auto authorization = projection->authorization;
  backend_.AsyncCall(&ProfileSyncBackend::AcknowledgeNativeBookmarks)
      .WithArgs(bookmark_adapter_->Capture(), authorization)
      .Then(
          base::BindOnce(&ProfileSyncService::OnBookmarkProjectionAcknowledged,
                         bookmark_weak_ptr_factory_.GetWeakPtr(), generation,
                         local_change, std::move(authorization)));
}

void ProfileSyncService::OnBookmarkProjectionAcknowledged(
    uint64_t generation,
    bool local_change,
    BookmarkSyncAuthorization authorization,
    bool success) {
  if (!bookmark_adapter_ || !bookmark_sync_enabled() ||
      generation != bookmark_adapter_->generation()) {
    return;
  }
  if (!authorization || !authorization.Run()) {
    bookmarks_seeded_ = false;
    SetBookmarkSyncIssue(BookmarkSyncIssue::kAuthorizationChanged);
    return;
  }
  if (!success) {
    bookmarks_seeded_ = false;
    SetBookmarkSyncIssue(BookmarkSyncIssue::kReconciliationFailed);
    return;
  }
  SetBookmarkSyncIssue(BookmarkSyncIssue::kNone);
  bookmark_adapter_->AcknowledgeCapture(generation);
  if (local_change) {
    SyncNow();
  }
}

}  // namespace ahoi::sync
