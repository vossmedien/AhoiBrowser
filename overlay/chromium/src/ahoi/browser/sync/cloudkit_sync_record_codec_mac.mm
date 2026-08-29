// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_record_codec_mac.h"

#import <Foundation/Foundation.h>

#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"
#include "ahoi/browser/sync/sync_payload_cryptor.h"
#include "ahoi/browser/sync/sync_serialization.h"
#include "base/strings/string_number_conversions.h"

namespace ahoi::sync {
namespace {

constexpr char kRecordType[] = "AhoiSyncRecord";
constexpr int64_t kTombstoneRetentionMillis = 30LL * 24 * 60 * 60 * 1000;

bool MetadataMatches(CKRecord* record, const SyncChange& change) {
  NSString* entity_id = record[@"entityID"];
  NSNumber* schema_version = record[@"schemaVersion"];
  NSNumber* physical = record[@"hlcPhysical"];
  NSNumber* submillisecond = record[@"hlcSubmillisecond"];
  NSNumber* logical = record[@"hlcLogical"];
  NSString* node_id = record[@"hlcNodeID"];
  NSString* originating_device = record[@"originatingDeviceID"];
  NSNumber* tombstone = record[@"isTombstone"];
  if (![entity_id isKindOfClass:[NSString class]] ||
      ![schema_version isKindOfClass:[NSNumber class]] ||
      ![physical isKindOfClass:[NSNumber class]] ||
      (submillisecond && ![submillisecond isKindOfClass:[NSNumber class]]) ||
      ![logical isKindOfClass:[NSNumber class]] ||
      ![node_id isKindOfClass:[NSString class]] ||
      ![originating_device isKindOfClass:[NSString class]] ||
      ![tombstone isKindOfClass:[NSNumber class]]) {
    return false;
  }
  const std::string expected_id = change.entity_id.AsLowercaseString();
  const bool expected_tombstone = change.kind == ChangeKind::kDelete;
  const int64_t unix_us = UnixMicroseconds(change.version.stamp);
  if (unix_us < 0) {
    return false;
  }
  const int64_t expected_submillisecond = unix_us % 1000;
  if (expected_id != ToString(record.recordID.recordName) ||
      expected_id != ToString(entity_id) ||
      change.version.model_version != schema_version.intValue ||
      UnixMilliseconds(change.version.stamp) != physical.longLongValue ||
      expected_submillisecond !=
          (submillisecond ? submillisecond.longLongValue : 0) ||
      change.version.stamp.logical != logical.unsignedIntValue ||
      change.version.stamp.device_tiebreak != ToString(node_id) ||
      change.version.stamp.device_tiebreak != ToString(originating_device) ||
      expected_tombstone != tombstone.boolValue) {
    return false;
  }
  if (!expected_tombstone) {
    return true;
  }
  NSString* tombstone_id = record[@"tombstoneEntityID"];
  NSNumber* deleted_physical = record[@"tombstoneDeletedPhysical"];
  NSNumber* deleted_submillisecond =
      record[@"tombstoneDeletedSubmillisecond"];
  NSNumber* deleted_logical = record[@"tombstoneDeletedLogical"];
  NSString* deleted_node = record[@"tombstoneDeletedNodeID"];
  NSString* deleted_by = record[@"tombstoneDeletedBy"];
  return [tombstone_id isKindOfClass:[NSString class]] &&
         [deleted_physical isKindOfClass:[NSNumber class]] &&
         (!deleted_submillisecond ||
          [deleted_submillisecond isKindOfClass:[NSNumber class]]) &&
         [deleted_logical isKindOfClass:[NSNumber class]] &&
         [deleted_node isKindOfClass:[NSString class]] &&
         [deleted_by isKindOfClass:[NSString class]] &&
         expected_id == ToString(tombstone_id) &&
         UnixMilliseconds(change.version.stamp) ==
             deleted_physical.longLongValue &&
         expected_submillisecond ==
             (deleted_submillisecond
                  ? deleted_submillisecond.longLongValue
                  : 0) &&
         change.version.stamp.logical == deleted_logical.unsignedIntValue &&
         change.version.stamp.device_tiebreak == ToString(deleted_node) &&
         change.version.stamp.device_tiebreak == ToString(deleted_by);
}

}  // namespace

CKRecord* EncodeCloudKitSyncRecord(const SyncChange& change,
                                   const std::string& sealed_payload,
                                   CKRecordID* record_id,
                                   CKRecord* server_record) {
  CKRecord* record = server_record
                         ? [server_record copy]
                         : [[CKRecord alloc] initWithRecordType:@(kRecordType)
                                                       recordID:record_id];
  record[@"entityID"] = ToNSString(change.entity_id.AsLowercaseString());
  record[@"schemaVersion"] = @(change.version.model_version);
  record[@"dataClass"] = DataClass(change.entity_type);
  const int64_t unix_us = UnixMicroseconds(change.version.stamp);
  if (unix_us < 0) {
    return nil;
  }
  const int64_t unix_ms = unix_us / 1000;
  const int64_t submillisecond = unix_us % 1000;
  record[@"hlcPhysical"] = @(unix_ms);
  record[@"hlcSubmillisecond"] = @(submillisecond);
  record[@"hlcLogical"] = @(change.version.stamp.logical);
  record[@"hlcNodeID"] = ToNSString(change.version.stamp.device_tiebreak);
  record[@"originatingDeviceID"] =
      ToNSString(change.version.stamp.device_tiebreak);
  const bool tombstone = change.kind == ChangeKind::kDelete;
  record[@"isTombstone"] = @(tombstone);
  if (tombstone) {
    record[@"tombstoneEntityID"] =
        ToNSString(change.entity_id.AsLowercaseString());
    record[@"tombstoneDeletedPhysical"] = @(unix_ms);
    record[@"tombstoneDeletedSubmillisecond"] = @(submillisecond);
    record[@"tombstoneDeletedLogical"] = @(change.version.stamp.logical);
    record[@"tombstoneDeletedNodeID"] =
        ToNSString(change.version.stamp.device_tiebreak);
    record[@"tombstoneDeletedBy"] =
        ToNSString(change.version.stamp.device_tiebreak);
    record[@"tombstonePurgeAfter"] = @(unix_ms + kTombstoneRetentionMillis);
  }
  NSData* data = [NSData dataWithBytes:sealed_payload.data()
                                length:sealed_payload.size()];
  record.encryptedValues[@"encryptedValue"] = data;
  return record;
}

std::optional<SyncChange> DecodeCloudKitSyncRecord(
    CKRecord* record,
    SyncPayloadCryptor& cryptor) {
  if (![record.recordType isEqualToString:@(kRecordType)]) {
    return std::nullopt;
  }
  NSString* raw_type = record[@"dataClass"];
  std::optional<EntityType> type = EntityTypeForDataClass(raw_type);
  NSData* encrypted = record.encryptedValues[@"encryptedValue"];
  if (!type || ![encrypted isKindOfClass:[NSData class]]) {
    return std::nullopt;
  }
  std::string envelope(static_cast<const char*>(encrypted.bytes),
                       encrypted.length);
  std::optional<std::string> payload = cryptor.Open(envelope);
  if (!payload) {
    return std::nullopt;
  }
  SyncRecord decoded;
  if (!DeserializeRecord(*type, *payload, &decoded)) {
    return std::nullopt;
  }
  SyncChange change{
      .mutation_id =
          "cloud:" + ToString(record.recordID.recordName) + ":" +
          base::NumberToString(GetVersion(decoded).stamp.physical_time_us) +
          ":" + base::NumberToString(GetVersion(decoded).stamp.logical) + ":" +
          GetVersion(decoded).stamp.device_tiebreak,
      .entity_type = *type,
      .entity_id = GetEntityId(decoded),
      .kind = IsTombstone(decoded) ? ChangeKind::kDelete : ChangeKind::kUpsert,
      .version = GetVersion(decoded),
      .payload = std::move(*payload)};
  if (!MetadataMatches(record, change) ||
      !ValidateChangeEnvelope(change, &decoded)) {
    return std::nullopt;
  }
  return change;
}

}  // namespace ahoi::sync
