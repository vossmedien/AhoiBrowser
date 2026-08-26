// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_UTIL_MAC_H_
#define AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_UTIL_MAC_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ahoi/browser/sync/hybrid_logical_clock.h"
#include "ahoi/browser/sync/sync_model.h"

#ifdef __OBJC__
@class NSError;
@class NSString;

namespace ahoi::sync {

NSString* ToNSString(std::string_view value);
std::string ToString(NSString* value);
NSString* DataClass(EntityType type);
std::optional<EntityType> EntityTypeForDataClass(NSString* value);
std::string SafeCloudKitError(NSError* error);
int64_t UnixMilliseconds(const HlcStamp& stamp);

}  // namespace ahoi::sync
#endif  // __OBJC__

#endif  // AHOI_BROWSER_SYNC_CLOUDKIT_SYNC_UTIL_MAC_H_
