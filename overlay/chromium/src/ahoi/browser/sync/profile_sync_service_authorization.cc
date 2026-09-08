// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/functional/bind.h"

namespace ahoi::sync {

SyncAuthorization ProfileSyncService::StartProfileAuthorization() {
  RevokeProfileAuthorization();
  profile_scope_cancelled_ = std::make_shared<std::atomic<bool>>(false);
  return base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> cancelled) {
        return !cancelled->load(std::memory_order_acquire);
      },
      profile_scope_cancelled_);
}

void ProfileSyncService::RevokeProfileAuthorization() {
  profile_scope_cancelled_->store(true, std::memory_order_release);
}

}  // namespace ahoi::sync
