// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_SIMPLE_URL_LOADER_CLIENT_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_SIMPLE_URL_LOADER_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/extensions/ubo_network_client.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Profile;

namespace network {
class SimpleURLLoader;
namespace mojom {
class URLResponseHead;
}
}  // namespace network

namespace net {
struct RedirectInfo;
}

namespace ahoi::extensions {

class UboSimpleUrlLoaderClient final : public UboNetworkClient {
 public:
  explicit UboSimpleUrlLoaderClient(Profile* profile);
  ~UboSimpleUrlLoaderClient() override;

  UboSimpleUrlLoaderClient(const UboSimpleUrlLoaderClient&) = delete;
  UboSimpleUrlLoaderClient& operator=(const UboSimpleUrlLoaderClient&) = delete;

  void FetchCatalog(const GURL& exact_url,
                    UboCatalogDownloadCallback callback) override;
  void FetchPackage(const GURL& exact_url,
                    UboDownloadProgressCallback progress,
                    UboPackageDownloadCallback callback) override;
  void Cancel() override;

 private:
  enum class RequestKind { kCatalog, kPackage };

  bool Begin(const GURL& exact_url, RequestKind kind);
  void OnRedirect(const GURL& before,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* removed_headers);
  void OnCatalogComplete(std::optional<std::string> body);
  void OnPackageComplete(base::FilePath path);
  UboNetworkError ClassifyFailure() const;

  raw_ptr<Profile> profile_;
  GURL expected_url_;
  RequestKind kind_ = RequestKind::kCatalog;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  UboCatalogDownloadCallback catalog_callback_;
  UboPackageDownloadCallback package_callback_;
  base::WeakPtrFactory<UboSimpleUrlLoaderClient> weak_factory_{this};
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_SIMPLE_URL_LOADER_CLIENT_H_
