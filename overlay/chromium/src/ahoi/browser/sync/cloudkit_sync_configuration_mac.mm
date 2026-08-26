// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import <Foundation/Foundation.h>

#include "ahoi/browser/sync/cloudkit_sync_configuration_mac.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace ahoi::sync {
namespace {

std::string BundleString(NSString* key) {
  id value = [[NSBundle mainBundle] objectForInfoDictionaryKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return {};
  }
  std::string result = [static_cast<NSString*>(value) UTF8String];
  base::TrimWhitespaceASCII(result, base::TRIM_ALL, &result);
  if (result.empty() || base::StartsWith(result, "$(") ||
      base::StartsWith(result, "${")) {
    return {};
  }
  return result;
}

}  // namespace

bool CloudKitSyncConfigurationMac::IsTransportConfigured() const {
  return base::StartsWith(container_identifier, "iCloud.") &&
         container_identifier.size() > 7 && !zone_name.empty();
}

bool CloudKitSyncConfigurationMac::IsE2EKeyConfigured() const {
  return !keychain_service.empty() && !keychain_account.empty() &&
         key_version > 0;
}

std::optional<CloudKitSyncConfigurationMac>
CloudKitSyncConfigurationMac::FromMainBundle() {
  CloudKitSyncConfigurationMac result;
  result.container_identifier = BundleString(@"AHOI_CLOUDKIT_CONTAINER_ID");
  const std::string zone = BundleString(@"AHOI_CLOUDKIT_ZONE_NAME");
  if (!zone.empty()) {
    result.zone_name = zone;
  }
  result.subscription_identifier =
      BundleString(@"AHOI_CLOUDKIT_SUBSCRIPTION_ID");
  result.keychain_service = BundleString(@"AHOI_SYNC_KEYCHAIN_SERVICE");
  result.keychain_account = BundleString(@"AHOI_SYNC_KEYCHAIN_ACCOUNT");
  result.keychain_access_group =
      BundleString(@"AHOI_SYNC_KEYCHAIN_ACCESS_GROUP");
  const std::string version = BundleString(@"AHOI_SYNC_KEY_VERSION");
  unsigned parsed_version = 0;
  if (base::StringToUint(version, &parsed_version)) {
    result.key_version = parsed_version;
  }
  if (!result.IsTransportConfigured()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace ahoi::sync
