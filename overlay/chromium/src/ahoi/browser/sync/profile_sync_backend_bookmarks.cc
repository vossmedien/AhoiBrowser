// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/sync/bookmark_sync_journal.h"
#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "base/functional/bind.h"

namespace ahoi::sync {

void ProfileSyncBackend::ResetBookmarkAuthorizationScope(bool renew) {
  bookmark_scope_cancelled_->store(true, std::memory_order_release);
  if (renew) {
    bookmark_scope_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  }
}

BookmarkSyncAuthorization ProfileSyncBackend::CaptureBookmarkAuthorization() {
  if (!store_ || !transport_enabled_ || !bookmark_sync_enabled_ ||
      bookmark_scope_cancelled_->load(std::memory_order_acquire)) {
    return {};
  }
  BookmarkSyncAuthorization provider_authorization;
  if (provider_) {
    provider_authorization = provider_->GetBookmarkSyncAuthorization();
    if (!provider_authorization || !provider_authorization.Run()) {
      return {};  // Immediate provider revocation, not delayed UI status.
    }
  }
  // Provider-free local preparation is allowed only by the explicit local
  // opt-ins. A provider that exists but cannot supply its scope is denied
  // above.
  return base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> cancelled,
         BookmarkSyncAuthorization provider_authorization) {
        return !cancelled->load(std::memory_order_acquire) &&
               (!provider_authorization || provider_authorization.Run());
      },
      bookmark_scope_cancelled_, std::move(provider_authorization));
}

std::optional<SyncStateSnapshot> ProfileSyncBackend::SetBookmarkSyncEnabled(
    bool enabled) {
  ResetBookmarkAuthorizationScope(enabled && transport_enabled_);
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
  auto authorization = CaptureBookmarkAuthorization();
  if (!authorization || !authorization.Run()) {
    return std::nullopt;
  }
  auto projection = BookmarkSyncJournal(store_.get())
                        .ReconcileLocal(snapshot, &clock_, authorization);
  if (projection) {
    projection->authorization = std::move(authorization);
  }
  return projection;
}

std::optional<BookmarkSyncProjection>
ProfileSyncBackend::ReadBookmarkProjection() {
  auto authorization = CaptureBookmarkAuthorization();
  if (!authorization || !authorization.Run()) {
    return std::nullopt;
  }
  auto projection =
      BookmarkSyncJournal(store_.get()).ReadProjection(authorization);
  if (projection) {
    projection->authorization = std::move(authorization);
  }
  return projection;
}

bool ProfileSyncBackend::AcknowledgeNativeBookmarks(
    NativeBookmarkSnapshot snapshot,
    BookmarkSyncAuthorization authorization) {
  auto current = CaptureBookmarkAuthorization();
  if (!authorization || !current) {
    return false;
  }
  // The ACK belongs to the scope that produced/applied the projection. A later
  // reapproval must not make an old receipt reply authoritative again.
  auto valid = base::BindRepeating(
      [](BookmarkSyncAuthorization original,
         BookmarkSyncAuthorization current) {
        return original.Run() && current.Run();
      },
      std::move(authorization), std::move(current));
  return valid.Run() && BookmarkSyncJournal(store_.get())
                            .AcknowledgeNativeProjection(snapshot, valid);
}

}  // namespace ahoi::sync
