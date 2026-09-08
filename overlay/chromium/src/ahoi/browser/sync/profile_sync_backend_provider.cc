// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include "ahoi/browser/sync/profile_sync_backend.h"
#include "ahoi/browser/sync/sync_pump.h"
#include "base/functional/bind.h"
#if BUILDFLAG(IS_MAC)
#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_key_bootstrap_mac.h"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac.h"
#endif

namespace ahoi::sync {

bool ProfileSyncBackend::RetrySyncKeySetup() {
#if BUILDFLAG(IS_MAC)
  if (!ProfileScopeActive() || !transport_enabled_ || !store_ ||
      (key_bootstrap_ && key_bootstrap_->pending()) ||
      key_setup_issue_ == "key_setup_account_changed" ||
      (provider_ && (provider_->IsAccountTransitionPending() ||
                     provider_->IsZoneRecoveryPending()))) {
    return false;
  }
  pump_.reset();
  provider_.reset();
  key_bootstrap_.reset();
  InitializeProviderIfAvailable();
  return key_bootstrap_ != nullptr;
#else
  return false;
#endif
}

void ProfileSyncBackend::InitializeProviderIfAvailable() {
  if (!ProfileScopeActive() || !transport_enabled_ || !store_ || provider_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  std::optional<CloudKitSyncConfigurationMac> configuration =
      CloudKitSyncConfigurationMac::FromMainBundle();
  if (!configuration) {
    return;
  }
  if (key_bootstrap_) {
    return;
  }
  key_setup_issue_ = "key_setup_in_progress";
  key_bootstrap_ = CloudKitSyncKeyBootstrapMac::Start(
      *configuration, profile_authorization_,
      base::BindRepeating(&ProfileSyncBackend::OnKeyBootstrapResult,
                          weak_ptr_factory_.GetWeakPtr()));
#endif
}

#if BUILDFLAG(IS_MAC)
void ProfileSyncBackend::OnKeyBootstrapResult(
    MacSyncKeyBootstrapResult result) {
  if (!ProfileScopeActive() || !transport_enabled_ || !store_) {
    return;
  }
  key_setup_issue_ = result.issue;
  auto configuration = std::move(result.configuration);
  if (!configuration.IsTransportConfigured() || !result.cryptor ||
      !result.authorization || !result.authorization.Run()) {
    // Keep an existing provider alive to persist its account-change/recovery
    // event. Its permanently revoked key lease already fences domain traffic.
    auto waiting = std::exchange(key_setup_waiters_, {});
    for (auto& callback : waiting) {
      std::move(callback).Run(CurrentState());
    }
    return;
  }
  configuration.verified_key_sha256 = result.key_sha256;
  configuration.verified_key_authorization = std::move(result.authorization);
  provider_ = CloudKitSyncProviderMac::Create(
      configuration,
      database_path_.DirName().AppendASCII("cksync-format3.state"),
      std::move(result.cryptor), bookmark_sync_enabled_, profile_authorization_,
      setting_authorization_);
  if (provider_) {
    if (provider_->IsBookmarkConsentRevoked()) {
      bookmark_sync_enabled_ = false;
    }
    // A former provider-free projection cannot inherit a newly available
    // provider/account's permission merely because local opt-in stayed true.
    ResetBookmarkAuthorizationScope(bookmark_sync_enabled_);
    pump_ = std::make_unique<SyncPump>(
        store_.get(), provider_.get(),
        SyncPump::Options{.bookmark_sync_enabled = bookmark_sync_enabled_});
  } else {
    key_setup_issue_ = "key_setup_provider_unavailable";
  }
  auto waiting = std::exchange(key_setup_waiters_, {});
  if (!waiting.empty()) {
    SyncNow(base::BindOnce(
        [](std::vector<base::OnceCallback<void(
               std::optional<SyncStateSnapshot>)>> callbacks,
           std::optional<SyncStateSnapshot> snapshot) {
          for (auto& callback : callbacks) {
            std::move(callback).Run(snapshot);
          }
        },
        std::move(waiting)));
  }
}
#endif

}  // namespace ahoi::sync
