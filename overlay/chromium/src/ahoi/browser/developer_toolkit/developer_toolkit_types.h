// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TYPES_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "url/origin.h"

namespace ahoi {

// Actions are intentionally closed. The developer toolkit never accepts
// script text from a caller, which keeps the browser surface auditable and
// prevents an accidental arbitrary-JavaScript execution API.
enum class DocumentAction {
  kToggleCss,
  kTogglePasswordFields,
  kToggleStructureOutlines,
  kToggleAltTitleLabels,
  kToggleDocumentMetadata,
  kClearSessionStorage,
  kResetDocumentModifications,
};

// Public command identifiers stay closed so every action remains auditable
// and can be dispatched without accepting arbitrary script or URL input.
enum class DeveloperAction {
  kClearCache,
  kClearSiteData,
  kToggleCss,
  kTogglePasswordFields,
  kToggleStructureOutlines,
  kToggleAltTitleLabels,
  kToggleDocumentMetadata,
  kResetDocumentModifications,
  kToggleJavaScript,
  kToggleImages,
  kCaptureVisibleScreenshot,
  kCaptureFullPageScreenshot,
};

enum class DeveloperActionStatus {
  kExecuted,
  kRejectedUnsupportedTarget,
  kUnavailable,
};

enum class DeveloperActivation : uint32_t {
  kCss = 1u << 0,
  kJavaScript = 1u << 1,
  kImages = 1u << 2,
  kHeaders = 1u << 3,
  kCacheOff = 1u << 4,
  kPasswordFields = 1u << 5,
  kStructureOutlines = 1u << 6,
  kAltTitleLabels = 1u << 7,
  kDocumentMetadata = 1u << 8,
};

struct DeveloperActivationState {
  uint32_t mask = 0;

  bool Has(DeveloperActivation activation) const {
    return (mask & static_cast<uint32_t>(activation)) != 0;
  }
  void Set(DeveloperActivation activation, bool active) {
    if (active) {
      mask |= static_cast<uint32_t>(activation);
    } else {
      mask &= ~static_cast<uint32_t>(activation);
    }
  }
  void Toggle(DeveloperActivation activation) {
    mask ^= static_cast<uint32_t>(activation);
  }
  void Reset() { mask = 0; }

  bool operator==(const DeveloperActivationState&) const = default;
};

struct DeveloperActionResult {
  DeveloperAction action;
  DeveloperActionStatus status;

  bool succeeded() const { return status == DeveloperActionStatus::kExecuted; }
  bool operator==(const DeveloperActionResult&) const = default;
};

enum class ContentSettingType {
  kJavaScript,
  kImages,
};

enum class ContentSettingValue {
  kAllow,
  kBlock,
};

enum class BrowsingDataScope {
  kCacheOnly,
  kFullSiteData,
};

enum class BrowsingDataTarget {
  kCurrentSite,
  kGlobal,
};

enum class BrowsingDataTimeRange {
  kLastHour,
  kLast24Hours,
  kLast7Days,
  kLast4Weeks,
  kAllTime,
};

// The bit values form a stable, toolkit-owned contract. A Chromium adapter
// can translate them to BrowsingDataRemover's DATA_TYPE_* mask without
// making this core depend on Profile or the browser UI layer.
enum class BrowsingDataType : uint32_t {
  kCache = 1u << 0,
  kCookies = 1u << 1,
  kLocalStorage = 1u << 2,
  kIndexedDb = 1u << 3,
  kServiceWorkers = 1u << 4,
  kSessionStorage = 1u << 5,
  kCacheStorage = 1u << 6,
};

constexpr uint32_t ToMask(BrowsingDataType type) {
  return static_cast<uint32_t>(type);
}

inline constexpr uint32_t kAllDeveloperBrowsingDataTypes =
    ToMask(BrowsingDataType::kCache) | ToMask(BrowsingDataType::kCookies) |
    ToMask(BrowsingDataType::kLocalStorage) |
    ToMask(BrowsingDataType::kIndexedDb) |
    ToMask(BrowsingDataType::kServiceWorkers) |
    ToMask(BrowsingDataType::kSessionStorage) |
    ToMask(BrowsingDataType::kCacheStorage);

struct BrowsingDataClearOptions {
  BrowsingDataTarget target = BrowsingDataTarget::kCurrentSite;
  BrowsingDataTimeRange time_range = BrowsingDataTimeRange::kAllTime;
  uint32_t data_type_mask = ToMask(BrowsingDataType::kCache);

  bool operator==(const BrowsingDataClearOptions&) const = default;
};

enum class BrowsingDataClearStatus {
  kSucceeded,
  kPartiallySucceeded,
  kFailed,
};

// Completion is expressed in the toolkit-owned bit domain so Chromium
// implementation details never leak into the UI. `options` is the immutable
// snapshot that was actually dispatched, not a reread of live controls.
struct BrowsingDataClearResult {
  BrowsingDataClearOptions options;
  uint32_t failed_data_type_mask = 0;

  uint32_t completed_data_type_mask() const {
    return options.data_type_mask & ~failed_data_type_mask;
  }
  BrowsingDataClearStatus status() const {
    if (failed_data_type_mask == 0) {
      return BrowsingDataClearStatus::kSucceeded;
    }
    return completed_data_type_mask() == 0
               ? BrowsingDataClearStatus::kFailed
               : BrowsingDataClearStatus::kPartiallySucceeded;
  }
  bool operator==(const BrowsingDataClearResult&) const = default;
};

using BrowsingDataClearCallback =
    base::OnceCallback<void(BrowsingDataClearResult)>;

struct BrowsingDataClearRequest {
  // Present for current-site requests and absent for global requests.
  std::optional<url::Origin> origin;
  BrowsingDataTarget target;
  BrowsingDataTimeRange time_range;
  uint32_t data_type_mask;

  bool operator==(const BrowsingDataClearRequest&) const = default;
};

struct DocumentActionScript {
  DocumentAction action;
  std::string_view source;

  bool operator==(const DocumentActionScript&) const = default;
};

struct ContentSettingToggle {
  url::Origin origin;
  ContentSettingType type;
  ContentSettingValue value;

  bool operator==(const ContentSettingToggle&) const = default;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_TOOLKIT_TYPES_H_
