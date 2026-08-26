// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_RECORD_CODEC_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_RECORD_CODEC_MAC_H_

#import <CloudKit/CloudKit.h>

#include <optional>
#include <string>

#include "ahoi/browser/sync/sync_provider.h"

namespace ahoi::sync {

class SyncPayloadCryptor;

// Converts between authenticated Ahoi sync envelopes and CloudKit records.
// The transport owns conflict selection; this codec validates every duplicated
// metadata field before releasing decrypted payloads to the sync store.
CKRecord* EncodeCloudKitSyncRecord(const SyncChange& change,
                                   const std::string& sealed_payload,
                                   CKRecordID* record_id,
                                   CKRecord* server_record);

std::optional<SyncChange> DecodeCloudKitSyncRecord(CKRecord* record,
                                                   SyncPayloadCryptor& cryptor);

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_RECORD_CODEC_MAC_H_
