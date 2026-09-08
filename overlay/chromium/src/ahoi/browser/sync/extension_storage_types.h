// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_EXTENSION_STORAGE_TYPES_H_
#define AHOI_BROWSER_SYNC_EXTENSION_STORAGE_TYPES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/sync/sync_authorization.h"
#include "base/containers/span.h"
#include "base/uuid.h"

namespace ahoi::sync {

// Deliberately finite first catalogue. There is no arbitrary JSON, local
// storage, URL, key mapping, site list or extension-supplied schema in this
// API.
struct ExtensionStorageDescriptor {
  std::string_view extension_id;
  std::string_view reviewed_version;
  std::string_view key;
  bool default_value;
};

struct ExtensionStorageValue {
  std::string extension_id;
  std::string key;
  // Absent is an explicit native Remove/reset, not a missing capture result.
  std::optional<bool> value;
  friend bool operator==(const ExtensionStorageValue&,
                         const ExtensionStorageValue&) = default;
};

base::span<const ExtensionStorageDescriptor> GetExtensionStorageCatalog();
const ExtensionStorageDescriptor* FindExtensionStorageSetting(
    std::string_view extension_id,
    std::string_view key);
bool IsValidExtensionStorageValue(const ExtensionStorageValue& value);

struct NativeExtensionStorageSnapshot {
  bool complete = false;
  // For eligible, installed, reviewed-version CWS extensions only. Include
  // every supported key, even when absent. Never synthesize entries for a
  // missing/ineligible extension or seed absent values on a fresh profile.
  std::vector<ExtensionStorageValue> values;
};

enum class ExtensionStorageDisposition {
  kStored,  // Actual successful storage commit AND matching keyed readback.
  kDeferred,
  kUnsupported,
  kBlockedByPolicy,
  kFailed,
  kCancelled,
};

struct ExtensionStorageRequest {
  ExtensionStorageValue desired;
  base::Uuid operation_id;
  std::string revision;
  SyncAuthorization authorization;
};

struct ExtensionStorageResult {
  base::Uuid operation_id;
  std::string revision;
  ExtensionStorageDisposition disposition =
      ExtensionStorageDisposition::kUnsupported;
  std::optional<ExtensionStorageValue> readback;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_EXTENSION_STORAGE_TYPES_H_
