// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_provider.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "ahoi/browser/sync/sync_store.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_key_bootstrap_mac.h"
#endif

namespace ahoi::sync {

bool ProfileSyncBackend::KeySetupAccountChangeAwaitsConfirmation() {
  return key_setup_issue_ == "key_setup_account_changed" &&
         (!provider_ || !provider_->IsAccountTransitionPending());
}

bool ProfileSyncBackend::ResetForKeySetupAccountChange(
    bool allow_local_upload) {
  if (!store_ || !KeySetupAccountChangeAwaitsConfirmation()) {
    return false;
  }
  ResetBookmarkAuthorizationScope(transport_enabled_ && bookmark_sync_enabled_);
  if (store_->PrepareOutboxForCloudRecovery(allow_local_upload) !=
      SyncStore::Result::kOk) {
    return false;
  }
  // The provider's key lease was revoked by the account notification and can
  // never upload again; keeping it only turned both recovery buttons into
  // permanent no-ops. A later real CKSyncEngine account switch is reported
  // again by the next provider, which rehydrates the persisted engine state.
  pump_.reset();
  provider_.reset();
#if BUILDFLAG(IS_MAC)
  key_bootstrap_.reset();
#endif
  key_setup_issue_.clear();
  return true;
}

bool ProfileSyncBackend::ConfirmAccountTransition(bool allow_local_upload) {
  if (!store_ || !transport_enabled_ || !ProfileScopeActive()) {
    return false;
  }
#if BUILDFLAG(IS_MAC)
  if (KeySetupAccountChangeAwaitsConfirmation()) {
    // An account notification revoked the key-setup lease, either before the
    // domain provider existed or after it was created (the notification
    // observer outlives a successful bootstrap). Status reports this as a
    // pending account transition, so the UI offers the explicit upload/no-
    // upload choice. The provider-only path below requires the provider to
    // own the transition and returned false here, making both buttons
    // permanent no-ops. Preserve local records and apply the choice
    // transactionally before starting an independently verified new claim/key
    // lease. No old key or CloudKit record is copied or replaced here.
    const auto configuration =
        CloudKitSyncConfigurationMac::FromMainBundle(sync_namespace_);
    if (!configuration || !configuration->IsTransportConfigured() ||
        !configuration->IsE2EKeyConfigured()) {
      return false;
    }
    if (!ResetForKeySetupAccountChange(allow_local_upload)) {
      return false;
    }
    InitializeProviderIfAvailable();
    return key_bootstrap_ != nullptr;
  }
#endif
  if (!provider_ || !provider_->IsAccountTransitionPending()) {
    return false;
  }
  ResetBookmarkAuthorizationScope(transport_enabled_ && bookmark_sync_enabled_);
  if (store_->PrepareOutboxForCloudRecovery(allow_local_upload) !=
      SyncStore::Result::kOk) {
    return false;
  }
  const bool confirmed =
      provider_->ConfirmAccountTransition(allow_local_upload);
#if BUILDFLAG(IS_MAC)
  if (confirmed && key_bootstrap_) {
    // Never renew the former account's key lease. A confirmed transition still
    // needs a new, independently verified claim/key/account binding.
    pump_.reset();
    provider_.reset();
    key_bootstrap_.reset();
    InitializeProviderIfAvailable();
  }
#endif
  return confirmed;
}

bool ProfileSyncBackend::ConfirmZoneRecovery() {
  if (!provider_ || !store_ || !provider_->IsZoneRecoveryPending()) {
    return false;
  }
  ResetBookmarkAuthorizationScope(transport_enabled_ && bookmark_sync_enabled_);
  if (store_->PrepareOutboxForCloudRecovery(true) != SyncStore::Result::kOk) {
    return false;
  }
  return provider_->ConfirmZoneRecovery();
}

}  // namespace ahoi::sync
