// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/cloudkit_sync_util_mac.h"

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>

namespace ahoi::sync {
namespace {

constexpr int64_t kWindowsToUnixEpochMicros = 11644473600000000LL;

}  // namespace

NSString* ToNSString(std::string_view value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

std::string ToString(NSString* value) {
  return value ? std::string(value.UTF8String) : std::string();
}

NSString* DataClass(EntityType type) {
  switch (type) {
    case EntityType::kDevice:
      return @"device";
    case EntityType::kWorkspace:
      return @"workspace";
    case EntityType::kTreeNode:
      return @"treeNode";
    case EntityType::kHistoryEntry:
      return @"historyVisit";
    case EntityType::kRemoteTab:
      return @"deviceTab";
    case EntityType::kDeviceSession:
      return @"deviceSession";
    case EntityType::kRemoteCommand:
      return @"remoteCommand";
    case EntityType::kAppearance:
      return @"appearance";
    case EntityType::kPermittedSetting:
      return @"permittedSetting";
    case EntityType::kExtensionInventory:
      return @"extensionInventory";
    case EntityType::kDeveloperAsset:
      return @"developerAsset";
    case EntityType::kBookmark:
      return @"bookmark";
  }
}

std::optional<EntityType> EntityTypeForDataClass(NSString* value) {
  if ([value isEqualToString:@"device"]) {
    return EntityType::kDevice;
  }
  if ([value isEqualToString:@"workspace"]) {
    return EntityType::kWorkspace;
  }
  if ([value isEqualToString:@"treeNode"]) {
    return EntityType::kTreeNode;
  }
  if ([value isEqualToString:@"historyVisit"]) {
    return EntityType::kHistoryEntry;
  }
  if ([value isEqualToString:@"deviceTab"]) {
    return EntityType::kRemoteTab;
  }
  if ([value isEqualToString:@"deviceSession"]) {
    return EntityType::kDeviceSession;
  }
  if ([value isEqualToString:@"remoteCommand"]) {
    return EntityType::kRemoteCommand;
  }
  if ([value isEqualToString:@"appearance"]) {
    return EntityType::kAppearance;
  }
  if ([value isEqualToString:@"permittedSetting"]) {
    return EntityType::kPermittedSetting;
  }
  if ([value isEqualToString:@"extensionInventory"]) {
    return EntityType::kExtensionInventory;
  }
  if ([value isEqualToString:@"developerAsset"]) {
    return EntityType::kDeveloperAsset;
  }
  if ([value isEqualToString:@"bookmark"]) {
    return EntityType::kBookmark;
  }
  return std::nullopt;
}

std::string SafeCloudKitError(NSError* error) {
  if (!error) {
    return {};
  }
  if (![error.domain isEqualToString:CKErrorDomain]) {
    return "provider_error";
  }
  switch (static_cast<CKErrorCode>(error.code)) {
    case CKErrorNotAuthenticated:
    case CKErrorPermissionFailure:
      return "account_unavailable";
    case CKErrorNetworkUnavailable:
    case CKErrorNetworkFailure:
      return "network";
    case CKErrorQuotaExceeded:
      return "quota";
    case CKErrorRequestRateLimited:
      return "rate_limited";
    case CKErrorServiceUnavailable:
    case CKErrorZoneBusy:
      return "temporarily_unavailable";
    case CKErrorServerRejectedRequest:
    case CKErrorInternalError:
      return "server";
    default:
      return "provider_error";
  }
}

int64_t UnixMilliseconds(const HlcStamp& stamp) {
  return UnixMicroseconds(stamp) / 1000;
}

int64_t UnixMicroseconds(const HlcStamp& stamp) {
  return stamp.physical_time_us - kWindowsToUnixEpochMicros;
}

}  // namespace ahoi::sync
