// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_KEY_BOOTSTRAP_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_KEY_BOOTSTRAP_MAC_H_

#include <memory>
#include <string>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/sync_authorization.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"

#ifdef __OBJC__
@class CKRecord;
@class CKRecordZoneID;
#endif

namespace ahoi::sync {

struct MacSyncKeyBootstrapResult {
  CloudKitSyncConfigurationMac configuration;
  std::unique_ptr<SyncPayloadCryptor> cryptor;
  // Public SHA256 commitment of a random 256-bit key, never key bytes.
  std::string key_sha256;
  SyncAuthorization authorization;
  // Empty only when ready. Local status, not a wire/error payload from a peer.
  std::string issue;
};

// Short-lived CloudKit CONTROL operations, then the existing single domain
// CKSyncEngine. No second sync database/engine, manual key injection or key
// replacement. Keep the object alive after ready: account notifications revoke
// the original authorization immediately, including a delayed native upload.
class CloudKitSyncKeyBootstrapMac {
 public:
  using Completion = base::RepeatingCallback<void(MacSyncKeyBootstrapResult)>;
  static std::unique_ptr<CloudKitSyncKeyBootstrapMac> Start(
      const CloudKitSyncConfigurationMac& configuration,
      SyncAuthorization authorization,
      Completion completion);
  ~CloudKitSyncKeyBootstrapMac();

  // Existing Sync cadence may check LOCAL Keychain arrival after a verified
  // remote claim. This does not rescan/recreate a zone or repeat a failed
  // claim.
  void CheckWaitingKey();
  bool pending() const;

 private:
  class Core;
  explicit CloudKitSyncKeyBootstrapMac(std::shared_ptr<Core> core);
  std::shared_ptr<Core> core_;
};

#ifdef __OBJC__
bool IsCloudKitKeyBootstrapRecord(CKRecord* record);
bool MatchesCloudKitKeyBootstrapRecord(CKRecord* record,
                                       CKRecordZoneID* zone,
                                       uint32_t key_version,
                                       const std::string& key_sha256);
#endif

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_KEY_BOOTSTRAP_MAC_H_
