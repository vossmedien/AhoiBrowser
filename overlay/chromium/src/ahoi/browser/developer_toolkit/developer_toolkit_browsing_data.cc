// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_toolkit_browsing_data.h"

#include <utility>

#include "ahoi/browser/developer_toolkit/developer_toolkit_target.h"
#include "content/public/browser/web_contents.h"

namespace ahoi {
namespace {

constexpr uint32_t kFullSiteDataMask =
    ToMask(BrowsingDataType::kCache) | ToMask(BrowsingDataType::kCookies) |
    ToMask(BrowsingDataType::kLocalStorage) |
    ToMask(BrowsingDataType::kIndexedDb) |
    ToMask(BrowsingDataType::kServiceWorkers) |
    ToMask(BrowsingDataType::kSessionStorage) |
    ToMask(BrowsingDataType::kCacheStorage);

uint32_t DataTypeMaskForScope(BrowsingDataScope scope) {
  switch (scope) {
    case BrowsingDataScope::kCacheOnly:
      return ToMask(BrowsingDataType::kCache);
    case BrowsingDataScope::kFullSiteData:
      return kFullSiteDataMask;
  }
}

}  // namespace

std::optional<BrowsingDataClearRequest> BuildBrowsingDataClearRequest(
    const GURL& active_url,
    BrowsingDataClearOptions options) {
  if (options.data_type_mask == 0 ||
      (options.data_type_mask & ~kAllDeveloperBrowsingDataTypes) != 0) {
    return std::nullopt;
  }
  if (options.target == BrowsingDataTarget::kCurrentSite &&
      !IsSupportedDeveloperTargetUrl(active_url)) {
    return std::nullopt;
  }

  return BrowsingDataClearRequest{
      .origin = options.target == BrowsingDataTarget::kCurrentSite
                    ? std::make_optional(url::Origin::Create(active_url))
                    : std::nullopt,
      .target = options.target,
      .time_range = options.time_range,
      .data_type_mask = options.data_type_mask,
  };
}

BrowsingDataClearOptions BrowsingDataOptionsForScope(BrowsingDataScope scope) {
  return BrowsingDataClearOptions{
      .target = BrowsingDataTarget::kCurrentSite,
      .time_range = BrowsingDataTimeRange::kAllTime,
      .data_type_mask = DataTypeMaskForScope(scope),
  };
}

BrowsingDataController::BrowsingDataController(
    std::unique_ptr<BrowsingDataRemovalAdapter> adapter)
    : adapter_(std::move(adapter)) {}

BrowsingDataController::~BrowsingDataController() = default;

bool BrowsingDataController::ClearCache(
    const content::WebContents* web_contents,
    BrowsingDataClearCallback callback) {
  return ClearData(web_contents,
                   BrowsingDataOptionsForScope(BrowsingDataScope::kCacheOnly),
                   std::move(callback));
}

bool BrowsingDataController::ClearSiteData(
    const content::WebContents* web_contents,
    BrowsingDataClearCallback callback) {
  return ClearData(
      web_contents,
      BrowsingDataOptionsForScope(BrowsingDataScope::kFullSiteData),
      std::move(callback));
}

bool BrowsingDataController::ClearData(const content::WebContents* web_contents,
                                       BrowsingDataClearOptions options,
                                       BrowsingDataClearCallback callback) {
  if (!adapter_ || callback.is_null() ||
      !IsSupportedDeveloperTarget(web_contents)) {
    return false;
  }

  std::optional<BrowsingDataClearRequest> request =
      BuildBrowsingDataClearRequest(web_contents->GetLastCommittedURL(),
                                    options);
  if (!request) {
    return false;
  }

  const bool dispatched = adapter_->Remove(
      *request,
      base::BindOnce(
          [](BrowsingDataClearOptions completed_options,
             BrowsingDataClearCallback completed_callback,
             uint32_t failed_data_type_mask) {
            std::move(completed_callback)
                .Run(BrowsingDataClearResult{
                    .options = completed_options,
                    .failed_data_type_mask = failed_data_type_mask &
                                             completed_options.data_type_mask,
                });
          },
          options, std::move(callback)));
  return dispatched;
}

}  // namespace ahoi
