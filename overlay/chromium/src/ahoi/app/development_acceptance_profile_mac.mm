// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import <Foundation/Foundation.h>

#include "ahoi/app/startup_policy.h"

#include "base/apple/foundation_util.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"

namespace ahoi::startup {
namespace {
std::string BundleString(NSString* key) {
  id value = [[NSBundle mainBundle] objectForInfoDictionaryKey:key];
  return [value isKindOfClass:NSString.class]
             ? std::string([static_cast<NSString*>(value) UTF8String])
             : std::string();
}
}  // namespace

bool ApplyDevelopmentAcceptanceProfile(base::CommandLine& command_line) {
  const std::string scope = BundleString(@"AHOI_SYNC_ACCEPTANCE_SCOPE_ID");
  const std::string binding =
      BundleString(@"AHOI_SYNC_ACCEPTANCE_SCOPE_SHA256");
  const std::string key_account = BundleString(@"AHOI_SYNC_KEYCHAIN_ACCOUNT");
  if (scope.empty() && binding.empty() &&
      !base::StartsWith(key_account, "payload-key.acceptance-")) {
    return true;  // Ordinary provider-free/CloudKit products retain their
                  // profiles.
  }
  if (!base::Uuid::ParseLowercase(scope).is_valid() || scope[14] != '4' ||
      binding.size() != 64 ||
      binding.find_first_not_of("0123456789abcdef") != std::string::npos ||
      BundleString(@"AhoiBuildProfile") != "dev" ||
      BundleString(@"AhoiMacCloudKitSigningProfile") !=
          "cloudkit-development" ||
      BundleString(@"AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT") != "Development" ||
      BundleString(@"AHOI_CLOUDKIT_ZONE_NAME") !=
          "AhoiSyncAcceptance-" + scope ||
      BundleString(@"AHOI_CLOUDKIT_SUBSCRIPTION_ID") !=
          "AhoiSyncAcceptanceSubscription-" + scope ||
      key_account != "payload-key.acceptance-" + scope) {
    return false;
  }
  NSArray<NSString*>* directories = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  if (directories.count != 1) {
    return false;
  }
  const base::FilePath support = base::apple::NSStringToFilePath(
      directories.firstObject.stringByResolvingSymlinksInPath);
  const base::FilePath family =
      support.AppendASCII("AhoiBrowser Sync Acceptance");
  const base::FilePath scope_root = family.AppendASCII(scope);
  base::FilePath requested = command_line.GetSwitchValuePath("user-data-dir");
  if (requested.empty()) {
    requested = scope_root.AppendASCII("MacA");
  }
  // Two explicit local clients support the same live acceptance scope. No
  // caller path may reach the real Default, another scope or an arbitrary
  // store.
  if ((requested != scope_root.AppendASCII("MacA") &&
       requested != scope_root.AppendASCII("MacB")) ||
      (command_line.HasSwitch("profile-directory") &&
       command_line.GetSwitchValueASCII("profile-directory") != "Default") ||
      base::IsLink(family) || base::IsLink(scope_root) ||
      base::IsLink(requested) ||
      base::IsLink(requested.AppendASCII("Default"))) {
    return false;
  }
  if (!base::CreateDirectory(requested) ||
      base::MakeAbsoluteFilePath(requested) != requested) {
    return false;
  }
  command_line.RemoveSwitch("user-data-dir");
  command_line.AppendSwitchPath("user-data-dir", requested);
  return true;
}

}  // namespace ahoi::startup
