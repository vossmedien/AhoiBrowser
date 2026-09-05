// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_H_

#include <memory>

#include "ahoi/browser/sync/sync_provider.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"

#ifdef __OBJC__
@class CKRecord;
#endif

namespace ahoi::sync {

struct CloudKitSyncConfigurationMac;
class CloudKitSyncProviderMacTest;
class CloudKitBookmarkSyncConsentTest;
class SyncPayloadCryptor;

// Objective-C++ CKSyncEngine adapter for the private database/custom zone.
// Creation fails closed when macOS, container configuration, or the externally
// provisioned E2E key is unavailable.
class CloudKitSyncProviderMac final : public SyncProvider {
 public:
  class Core;
  // Category approval must be supplied before cached records can be decoded.
  // The default does not inherit the caller's global sync setting.
  static std::unique_ptr<CloudKitSyncProviderMac> Create(
      const CloudKitSyncConfigurationMac& configuration,
      const base::FilePath& state_path,
      std::unique_ptr<SyncPayloadCryptor> cryptor,
      bool bookmark_sync_enabled = false);

  ~CloudKitSyncProviderMac() override;
  void Upload(std::vector<SyncChange> changes,
              UploadCallback callback) override;
  void Download(std::string change_token, DownloadCallback callback) override;
  void SetBookmarkSyncEnabled(bool enabled) override;
  bool IsBookmarkConsentRevoked() override;
  BookmarkSyncAuthorization GetBookmarkSyncAuthorization() override;
  bool IsAccountTransitionPending() override;
  bool IsZoneRecoveryPending() override;
  bool ConfirmAccountTransition(bool allow_local_upload) override;
  bool ConfirmZoneRecovery() override;

 private:
  friend class CloudKitSyncProviderMacTest;
  friend class CloudKitBookmarkSyncConsentTest;

  explicit CloudKitSyncProviderMac(std::shared_ptr<Core> core);
  static std::unique_ptr<CloudKitSyncProviderMac> CreateForTesting(
      const base::FilePath& state_path);
  base::RepeatingCallback<bool()> MakeDelayedCacheWriteForTesting();
#ifdef __OBJC__
  static std::unique_ptr<CloudKitSyncProviderMac> CreateForConsentTesting(
      const base::FilePath& state_path,
      std::unique_ptr<SyncPayloadCryptor> cryptor,
      bool bookmark_sync_enabled = false);
  void ReceiveRecordForTesting(CKRecord* record);
  base::RepeatingCallback<bool()> MakeDelayedRecordDeliveryForTesting(
      CKRecord* record);
  void ReadCachedChangesForTesting(std::string token,
                                   DownloadCallback callback);
  void AccountChangedForTesting();
#endif
  std::shared_ptr<Core> core_;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_PROVIDER_MAC_H_
