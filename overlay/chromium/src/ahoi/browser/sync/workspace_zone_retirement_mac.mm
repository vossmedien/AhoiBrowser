// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"
#include "ahoi/browser/sync/sync_namespace.h"
#include "ahoi/browser/sync/workspace_zone_retirement.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/sequenced_task_runner.h"

// WS-ISO-20: runtime-unverified. The zone delete and the Keychain deletes are
// exercised only by a real signed CloudKit build with an iCloud account.

namespace ahoi::sync {
namespace {

bool IsGoneError(NSError* error) {
  return error.code == CKErrorZoneNotFound ||
         error.code == CKErrorUserDeletedZone;
}

bool ZoneGone(NSError* error, CKRecordZoneID* zone) {
  if (!error) {
    return true;
  }
  if (IsGoneError(error)) {
    return true;
  }
  if (error.code == CKErrorPartialFailure) {
    NSDictionary* partial = error.userInfo[CKPartialErrorsByItemIDKey];
    NSError* item = [partial isKindOfClass:[NSDictionary class]]
                        ? partial[zone]
                        : nil;
    return [item isKindOfClass:[NSError class]] && IsGoneError(item);
  }
  return false;
}

bool DeleteKeychainItem(const CloudKitSyncConfigurationMac& configuration,
                        const std::string& account,
                        bool synchronizable) {
  NSMutableDictionary* query = [@{
    (__bridge NSString*)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge NSString*)kSecAttrService :
        base::SysUTF8ToNSString(configuration.keychain_service),
    (__bridge NSString*)kSecAttrAccount : base::SysUTF8ToNSString(account),
    (__bridge NSString*)kSecUseDataProtectionKeychain : @YES,
    (__bridge NSString*)kSecAttrSynchronizable : @(synchronizable),
  } mutableCopy];
  if (!configuration.keychain_access_group.empty()) {
    query[(__bridge NSString*)kSecAttrAccessGroup] =
        base::SysUTF8ToNSString(configuration.keychain_access_group);
  }
  const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
  return status == errSecSuccess || status == errSecItemNotFound;
}

// Retires the Workspace's key: the synchronizable canonical item (removed
// from iCloud Keychain on every device) and this device's bootstrap
// pending/journal items, named like keychain_sync_bootstrap_mac.mm.
bool RetireKey(const CloudKitSyncConfigurationMac& configuration) {
  if (!configuration.IsE2EKeyConfigured()) {
    return true;  // No key family configured, so none was ever stored.
  }
  const std::string& account = configuration.keychain_account;
  const std::string version = base::NumberToString(configuration.key_version);
  bool ok = DeleteKeychainItem(configuration, account, /*synchronizable=*/true);
  ok &= DeleteKeychainItem(configuration,
                           account + ".bootstrap-pending.v" + version,
                           /*synchronizable=*/false);
  ok &= DeleteKeychainItem(configuration,
                           account + ".bootstrap-journal.v" + version,
                           /*synchronizable=*/false);
  return ok;
}

}  // namespace

void RetireWorkspaceZone(const base::Uuid& workspace_id,
                         base::OnceCallback<void(bool)> done) {
  const std::optional<SyncNamespace> sync_namespace =
      SyncNamespace::ForSeparatedWorkspace(workspace_id);
  const std::optional<CloudKitSyncConfigurationMac> main =
      CloudKitSyncConfigurationMac::FromMainBundle();
  std::optional<CloudKitSyncConfigurationMac> configuration;
  if (sync_namespace) {
    configuration = CloudKitSyncConfigurationMac::FromMainBundle(
        *sync_namespace);
  }
  // Fail closed: never the main zone or the main key account.
  if (!sync_namespace || sync_namespace->is_main() || !main ||
      !configuration || configuration->zone_name == main->zone_name ||
      configuration->keychain_account == main->keychain_account ||
      configuration->zone_name.find(kWorkspaceZoneInfix) ==
          std::string::npos) {
    std::move(done).Run(false);
    return;
  }
  CKContainer* container = [CKContainer
      containerWithIdentifier:base::SysUTF8ToNSString(
                                  configuration->container_identifier)];
  if (!container) {
    std::move(done).Run(false);
    return;
  }
  CKRecordZoneID* zone = [[CKRecordZoneID alloc]
      initWithZoneName:base::SysUTF8ToNSString(configuration->zone_name)
             ownerName:CKCurrentUserDefaultName];
  auto operation = [[CKModifyRecordZonesOperation alloc]
      initWithRecordZonesToSave:nil
          recordZoneIDsToDelete:@[ zone ]];
  operation.qualityOfService = NSQualityOfServiceUtility;
  const scoped_refptr<base::SequencedTaskRunner> runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  // Blocks copy their captures; the move-only callback travels in a
  // shared_ptr and runs exactly once on the calling sequence.
  auto completion =
      std::make_shared<base::OnceCallback<void(bool)>>(std::move(done));
  const CloudKitSyncConfigurationMac retired = *configuration;
  operation.modifyRecordZonesCompletionBlock =
      ^(NSArray<CKRecordZone*>* saved, NSArray<CKRecordZoneID*>* deleted,
        NSError* error) {
        // Delete the key only after the zone (and its claim record) is gone,
        // so no device keeps reading a zone whose key vanished.
        const bool retired_all = ZoneGone(error, zone) && RetireKey(retired);
        runner->PostTask(FROM_HERE,
                         base::BindOnce(std::move(*completion), retired_all));
      };
  [container.privateCloudDatabase addOperation:operation];
}

}  // namespace ahoi::sync
