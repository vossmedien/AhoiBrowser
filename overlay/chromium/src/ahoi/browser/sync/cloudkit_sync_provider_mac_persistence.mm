// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
#include "ahoi/browser/sync/cloudkit_sync_provider_mac_internal.h"

namespace ahoi::sync {
namespace {

std::optional<SyncChange> DecodeCachedChange(NSDictionary* item) {
  NSString* mutation_id = item[@"mutationID"];
  NSString* entity_id = item[@"entityID"];
  NSString* device = item[@"device"];
  NSString* payload = item[@"payload"];
  NSNumber* entity_type = item[@"entityType"];
  NSNumber* kind = item[@"kind"];
  NSNumber* model = item[@"model"];
  NSNumber* physical = item[@"physical"];
  NSNumber* logical = item[@"logical"];
  if (![mutation_id isKindOfClass:[NSString class]] ||
      ![entity_id isKindOfClass:[NSString class]] ||
      ![device isKindOfClass:[NSString class]] ||
      ![payload isKindOfClass:[NSString class]] ||
      ![entity_type isKindOfClass:[NSNumber class]] ||
      ![kind isKindOfClass:[NSNumber class]] ||
      ![model isKindOfClass:[NSNumber class]] ||
      ![physical isKindOfClass:[NSNumber class]] ||
      ![logical isKindOfClass:[NSNumber class]]) {
    return std::nullopt;
  }
  const int raw_type = entity_type.intValue;
  const int raw_kind = kind.intValue;
  if (raw_type < static_cast<int>(EntityType::kDevice) ||
      raw_type > static_cast<int>(EntityType::kBookmark) ||
      raw_kind < static_cast<int>(ChangeKind::kUpsert) ||
      raw_kind > static_cast<int>(ChangeKind::kDelete)) {
    return std::nullopt;
  }
  SyncChange change{
      .mutation_id = ToString(mutation_id),
      .entity_type = static_cast<EntityType>(raw_type),
      .entity_id = base::Uuid::ParseLowercase(ToString(entity_id)),
      .kind = static_cast<ChangeKind>(raw_kind),
      .version = {.model_version = model.intValue,
                  .stamp = {.physical_time_us = physical.longLongValue,
                            .logical = logical.unsignedIntValue,
                            .device_tiebreak = ToString(device)}},
      .payload = ToString(payload)};
  SyncRecord decoded;
  if (!ValidateChangeEnvelope(change, &decoded) &&
      !IsCloudKitQuarantineMarker(change)) {
    return std::nullopt;
  }
  return change;
}

}  // namespace

CKSyncEngineStateSerialization* CloudKitSyncProviderMac::Core::LoadState() {
  NSData* data =
      [NSData dataWithContentsOfFile:ToNSString(state_path_.value())];
  if (!data) {
    return nil;
  }
  NSError* error = nil;
  CKSyncEngineStateSerialization* state = [NSKeyedUnarchiver
      unarchivedObjectOfClass:[CKSyncEngineStateSerialization class]
                     fromData:data
                        error:&error];
  if (!state || error) {
    account_transition_pending_ = zone_recovery_pending_ = true;
    bookmark_sync_enabled_ = false;
    bookmark_consent_revoked_ = true;
    opaque_bookmark_records_.clear();
    legacy_bookmark_changes_.clear();
    PersistInbox();
    return nil;
  }
  return state;
}

void CloudKitSyncProviderMac::Core::PersistState(
    CKSyncEngineStateSerialization* state) {
  // A failed opaque cache write must not advance the engine checkpoint past
  // ciphertext that has not reached disk. Restart can refetch from the old one.
  if (account_transition_pending_ || zone_recovery_pending_ ||
      (inbox_persistence_failed_ && !PersistInbox())) {
    return;
  }
  NSError* error = nil;
  NSData* data = [NSKeyedArchiver archivedDataWithRootObject:state
                                       requiringSecureCoding:YES
                                                       error:&error];
  if (!data || error) {
    return;
  }
  base::CreateDirectory(state_path_.DirName());
  [data writeToFile:ToNSString(state_path_.value())
            options:NSDataWritingAtomic
              error:nil];
}

void CloudKitSyncProviderMac::Core::AcknowledgeLastDelivery(
    const std::string& change_token) {
  if (change_token.empty() || change_token != last_delivery_token_) {
    return;
  }
  for (const auto& [id, mutation_id] : last_delivery_mutations_) {
    auto current = fetched_changes_.find(id);
    if (current == fetched_changes_.end() ||
        current->second.mutation_id != mutation_id) {
      continue;
    }
    for (auto it = materialized_bookmark_keys_.begin();
         it != materialized_bookmark_keys_.end();) {
      if (it->second == id) {
        opaque_bookmark_records_.erase(it->first);
        legacy_bookmark_changes_.erase(it->first);
        bookmark_quarantine_ids_.erase(it->first);
        it = materialized_bookmark_keys_.erase(it);
      } else {
        ++it;
      }
    }
    fetched_changes_.erase(current);
  }
  last_delivery_mutations_.clear();
  last_delivery_token_.clear();
  PersistInbox();
}

void CloudKitSyncProviderMac::Core::LoadCachedChange(NSDictionary* item) {
  NSNumber* type = item[@"entityType"];
  if ([type isKindOfClass:[NSNumber class]] &&
      type.intValue == static_cast<int>(EntityType::kBookmark)) {
    // Older providers persisted decoded bookmark changes. Retain the original
    // dictionary without parsing its payload until this category is approved.
    NSString* entity_id = item[@"entityID"];
    if (!account_transition_pending_ &&
        [entity_id isKindOfClass:[NSString class]]) {
      legacy_bookmark_changes_[ToString(entity_id)] = item;
    }
    return;
  }
  if (auto change = DecodeCachedChange(item)) {
    fetched_changes_[change->entity_id.AsLowercaseString()] =
        std::move(*change);
  }
}

void CloudKitSyncProviderMac::Core::HydrateLegacyBookmarks() {
  lock_.AssertAcquired();
  if (!BookmarkAllowed()) {
    return;
  }
  for (const auto& [key, item] : legacy_bookmark_changes_) {
    if (materialized_bookmark_keys_.contains(key)) {
      continue;
    }
    auto change = DecodeCachedChange(item);
    if (!change) {
      change = MakeCloudKitQuarantineMarker(EntityType::kBookmark);
    }
    const std::string materialized_key = change->entity_id.AsLowercaseString();
    materialized_bookmark_keys_[key] = materialized_key;
    fetched_changes_[materialized_key] = std::move(*change);
  }
}

void CloudKitSyncProviderMac::Core::LoadInbox() {
  NSData* data =
      [NSData dataWithContentsOfFile:ToNSString(inbox_path_.value())];
  if (!data) {
    return;
  }
  NSError* error = nil;
  id root = [NSJSONSerialization JSONObjectWithData:data
                                            options:0
                                              error:&error];
  if (error || ![root isKindOfClass:[NSDictionary class]]) {
    account_transition_pending_ = zone_recovery_pending_ = true;
    bookmark_sync_enabled_ = false;
    bookmark_consent_revoked_ = true;
    return;
  }
  NSDictionary* dictionary = root;
  NSNumber* generation = dictionary[@"generation"];
  if ([generation isKindOfClass:[NSNumber class]]) {
    download_generation_ = generation.unsignedLongLongValue;
  }
  account_transition_pending_ =
      [dictionary[@"accountTransitionPending"] boolValue];
  zone_recovery_pending_ = [dictionary[@"zoneRecoveryPending"] boolValue];
  bookmark_consent_revoked_ = account_transition_pending_ ||
                              [dictionary[@"bookmarkConsentRevoked"] boolValue];
  if (bookmark_consent_revoked_) {
    bookmark_sync_enabled_ = false;
  }
  NSString* delivery_token = dictionary[@"lastDeliveryToken"];
  if ([delivery_token isKindOfClass:[NSString class]]) {
    last_delivery_token_ = ToString(delivery_token);
  }
  NSDictionary* delivered_mutations = dictionary[@"deliveredMutations"];
  if ([delivered_mutations isKindOfClass:[NSDictionary class]]) {
    for (id key in delivered_mutations) {
      id mutation = delivered_mutations[key];
      if ([key isKindOfClass:[NSString class]] &&
          [mutation isKindOfClass:[NSString class]]) {
        last_delivery_mutations_[ToString(key)] = ToString(mutation);
      }
    }
  }
  for (NSString* collection in @[ @"changes", @"legacyBookmarks" ]) {
    NSArray* changes = dictionary[collection];
    if (![changes isKindOfClass:[NSArray class]]) {
      continue;
    }
    for (id value in changes) {
      if ([value isKindOfClass:[NSDictionary class]]) {
        LoadCachedChange(value);
      }
    }
  }
  if (account_transition_pending_) {
    return;
  }
  NSArray* opaque = dictionary[@"opaqueBookmarks"];
  if (![opaque isKindOfClass:[NSArray class]]) {
    return;
  }
  for (id value in opaque) {
    if (![value isKindOfClass:[NSString class]]) {
      account_transition_pending_ = zone_recovery_pending_ = true;
      break;
    }
    NSData* archive = [[NSData alloc] initWithBase64EncodedString:value
                                                          options:0];
    NSError* archive_error = nil;
    CKRecord* record =
        archive ? [NSKeyedUnarchiver unarchivedObjectOfClass:[CKRecord class]
                                                    fromData:archive
                                                       error:&archive_error]
                : nil;
    if (!record || archive_error || !IsBookmarkRecord(record)) {
      account_transition_pending_ = zone_recovery_pending_ = true;
      break;
    }
    const std::string key = ToString(record.recordID.recordName);
    opaque_bookmark_records_[key] = record;
    legacy_bookmark_changes_.erase(key);
  }
  NSDictionary* quarantined = dictionary[@"bookmarkQuarantineIDs"];
  if ([quarantined isKindOfClass:[NSDictionary class]]) {
    for (id key in quarantined) {
      id value = quarantined[key];
      if (![key isKindOfClass:[NSString class]] ||
          ![value isKindOfClass:[NSString class]]) {
        continue;
      }
      const base::Uuid marker = base::Uuid::ParseLowercase(ToString(value));
      if (marker.is_valid() &&
          opaque_bookmark_records_.contains(ToString(key))) {
        bookmark_quarantine_ids_[ToString(key)] = marker;
      }
    }
  }
  if (account_transition_pending_) {
    bookmark_sync_enabled_ = false;
    bookmark_consent_revoked_ = true;
    opaque_bookmark_records_.clear();
    legacy_bookmark_changes_.clear();
    bookmark_quarantine_ids_.clear();
  }
}

bool CloudKitSyncProviderMac::Core::PersistInbox() {
  NSMutableArray* changes = [NSMutableArray array];
  for (const auto& [id, change] : fetched_changes_) {
    if (change.entity_type == EntityType::kBookmark) {
      continue;
    }
    [changes addObject:@{
      @"mutationID" : ToNSString(change.mutation_id),
      @"entityType" : @(static_cast<int>(change.entity_type)),
      @"entityID" : ToNSString(change.entity_id.AsLowercaseString()),
      @"kind" : @(static_cast<int>(change.kind)),
      @"model" : @(change.version.model_version),
      @"physical" : @(change.version.stamp.physical_time_us),
      @"logical" : @(change.version.stamp.logical),
      @"device" : ToNSString(change.version.stamp.device_tiebreak),
      @"payload" : ToNSString(change.payload),
    }];
  }
  NSMutableArray* opaque = [NSMutableArray array];
  for (const auto& [id, record] : opaque_bookmark_records_) {
    NSError* archive_error = nil;
    NSData* data = [NSKeyedArchiver archivedDataWithRootObject:record
                                         requiringSecureCoding:YES
                                                         error:&archive_error];
    if (!data || archive_error) {
      inbox_persistence_failed_ = true;
      return false;
    }
    [opaque addObject:[data base64EncodedStringWithOptions:0]];
  }
  NSMutableArray* legacy = [NSMutableArray array];
  for (const auto& [id, item] : legacy_bookmark_changes_) {
    [legacy addObject:item];
  }
  NSMutableDictionary* delivered_mutations = [NSMutableDictionary dictionary];
  for (const auto& [id, mutation] : last_delivery_mutations_) {
    delivered_mutations[ToNSString(id)] = ToNSString(mutation);
  }
  NSMutableDictionary* quarantined = [NSMutableDictionary dictionary];
  for (const auto& [key, marker] : bookmark_quarantine_ids_) {
    quarantined[ToNSString(key)] = ToNSString(marker.AsLowercaseString());
  }
  NSDictionary* root = @{
    @"generation" : @(download_generation_),
    @"accountTransitionPending" : @(account_transition_pending_),
    @"zoneRecoveryPending" : @(zone_recovery_pending_),
    // This is a revocation marker, never a persisted approval.
    @"bookmarkConsentRevoked" : @(bookmark_consent_revoked_),
    @"lastDeliveryToken" : ToNSString(last_delivery_token_),
    @"deliveredMutations" : delivered_mutations,
    @"changes" : changes,
    @"opaqueBookmarks" : opaque,
    @"legacyBookmarks" : legacy,
    @"bookmarkQuarantineIDs" : quarantined,
  };
  NSError* error = nil;
  NSData* data = [NSJSONSerialization dataWithJSONObject:root
                                                 options:0
                                                   error:&error];
  inbox_persistence_failed_ = !data || error ||
                              !base::CreateDirectory(inbox_path_.DirName()) ||
                              ![data writeToFile:ToNSString(inbox_path_.value())
                                         options:NSDataWritingAtomic
                                           error:nil];
  return !inbox_persistence_failed_;
}

}  // namespace ahoi::sync
#pragma clang diagnostic pop
