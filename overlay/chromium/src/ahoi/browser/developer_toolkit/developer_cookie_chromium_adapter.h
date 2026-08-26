// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_CHROMIUM_ADAPTER_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_CHROMIUM_ADAPTER_H_

#include <optional>

#include "net/cookies/cookie_partition_key.h"
#include "url/gurl.h"

namespace ahoi {

// Resolves the storage identity used by the production cookie adapter. Editing
// preserves an existing opaque partition key exactly; new CHIPS cookies use
// Chromium's storage-key factory and fail closed if partitioning is disabled.
std::optional<net::CookiePartitionKey> ResolveDeveloperCookiePartitionKey(
    const GURL& site_url,
    bool partitioned,
    const std::optional<net::CookiePartitionKey>& existing_partition_key);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_COOKIE_CHROMIUM_ADAPTER_H_
