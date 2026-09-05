// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/sync_provider.h"

namespace ahoi::sync {

void SyncProvider::SetBookmarkSyncEnabled(bool enabled) {}

bool SyncProvider::IsBookmarkConsentRevoked() {
  return false;
}

BookmarkSyncAuthorization SyncProvider::GetBookmarkSyncAuthorization() {
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
