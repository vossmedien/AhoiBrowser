// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_NETWORK_CLIENT_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_NETWORK_CLIENT_H_

#include <cstdint>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "url/gurl.h"

namespace ahoi::extensions {

inline constexpr int64_t kMaximumUboCatalogBytes = 64 * 1024;
inline constexpr int64_t kMaximumUboPackageBytes = 32 * 1024 * 1024;

enum class UboNetworkError {
  kInvalidRequest,
  kOffline,
  kRedirect,
  kResponseTooLarge,
  kUnexpectedResponse,
  kCancelled,
};

template <typename T>
using UboNetworkResult = base::expected<T, UboNetworkError>;

struct UboCatalogDownload {
  GURL final_url;
  std::string body;
};

struct UboPackageDownload {
  GURL final_url;
  base::FilePath path;
};

using UboCatalogDownloadCallback =
    base::OnceCallback<void(UboNetworkResult<UboCatalogDownload>)>;
using UboPackageDownloadCallback =
    base::OnceCallback<void(UboNetworkResult<UboPackageDownload>)>;
using UboDownloadProgressCallback = base::RepeatingCallback<void(uint64_t)>;

// A deliberately small seam around Chromium's URLLoader. Tests can be fully
// hermetic without weakening production URL, redirect, or response bounds.
class UboNetworkClient {
 public:
  virtual ~UboNetworkClient() = default;

  virtual void FetchCatalog(const GURL& exact_url,
                            UboCatalogDownloadCallback callback) = 0;
  virtual void FetchPackage(const GURL& exact_url,
                            UboDownloadProgressCallback progress,
                            UboPackageDownloadCallback callback) = 0;
  virtual void Cancel() = 0;
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_NETWORK_CLIENT_H_
