// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/bookmark_sync_journal.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"

namespace ahoi::sync {

std::optional<SyncStateSnapshot> ProfileSyncBackend::SetBookmarkSyncEnabled(
    bool enabled) {
  bookmark_sync_enabled_ = enabled;
  if (pump_) {
    pump_->SetBookmarkSyncEnabled(enabled);
  }
  if (provider_) {
    provider_->SetBookmarkSyncEnabled(enabled);
  }
  return CurrentState();
}

std::optional<BookmarkSyncProjection> ProfileSyncBackend::MergeLocalBookmarks(
    NativeBookmarkSnapshot snapshot) {
  if (!store_ || !bookmark_sync_enabled_) {
    return std::nullopt;
  }
  return BookmarkSyncJournal(store_.get()).ReconcileLocal(snapshot, &clock_);
}

std::optional<BookmarkSyncProjection>
ProfileSyncBackend::ReadBookmarkProjection() {
  if (!store_ || !bookmark_sync_enabled_) {
    return std::nullopt;
  }
  return BookmarkSyncJournal(store_.get()).ReadProjection();
}

bool ProfileSyncBackend::AcknowledgeNativeBookmarks(
    NativeBookmarkSnapshot snapshot) {
  return store_ && bookmark_sync_enabled_ &&
         BookmarkSyncJournal(store_.get())
             .AcknowledgeNativeProjection(snapshot);
}

}  // namespace ahoi::sync
