// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_provider.h"

#include <utility>

namespace ahoi::sync {

void SyncProvider::SetIncomingCallback(IncomingCallback callback) {}

void SyncProvider::ReadPendingChanges(std::string change_token,
                                      SyncAuthorization authorization,
                                      DownloadCallback callback) {
  std::move(callback).Run(false, {}, "temporarily_unavailable");
}

bool SyncProvider::AcknowledgeDownloaded(std::string change_token,
                                         SyncAuthorization authorization) {
  return authorization && authorization.Run();
}

void SyncProvider::SetBookmarkSyncEnabled(bool enabled) {}

bool SyncProvider::IsBookmarkConsentRevoked() {
  return false;
}

BookmarkSyncAuthorization SyncProvider::GetBookmarkSyncAuthorization() {
  return {};
}

SyncAuthorization SyncProvider::GetTransportAuthorization() {
  return {};
}

SyncAuthorization SyncProvider::GetPermittedSettingSyncAuthorization(
    const base::Uuid& record_id) {
  return {};
}

std::string SyncProvider::GetKeySetupIssue() {
  return {};
}

bool SyncProvider::IsAccountTransitionPending() {
  return false;
}

bool SyncProvider::IsZoneRecoveryPending() {
  return false;
}

bool SyncProvider::ConfirmAccountTransition(bool allow_local_upload) {
  return false;
}

bool SyncProvider::ConfirmZoneRecovery() {
  return false;
}

}  // namespace ahoi::sync
