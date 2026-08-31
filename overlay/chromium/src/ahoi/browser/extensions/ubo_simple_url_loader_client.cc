// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_simple_url_loader_client.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/extensions/ubo_product_config.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace ahoi::extensions {

namespace {

constexpr base::TimeDelta kRequestTimeout = base::Seconds(30);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("ahoi_ubo_verified_catalog", R"(
      semantics {
        sender: "AhoiBrowser uBlock Origin Classic installer"
        description:
          "Fetches a product-pinned signed catalog or, only after explicit "
          "user action, the exact Official GitHub release CRX pinned into the "
          "browser."
        trigger:
          "A user opens the uBlock Origin Classic installer and requests a "
          "check or download. An optional catalog-only check may run for an "
          "already installed and locally authorized copy."
        data: "No user data. Requests contain no cookies or credentials."
        destination: OTHER
        destination_other:
          "Pinned AhoiBrowser catalog origin or exact GitHub release asset"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Package installation is always user-confirmed. Periodic catalog "
          "checks only run after the extension is installed and authorized."
        policy_exception_justification:
          "The production endpoint is a compile-time trust root."
      })");

bool IsExactHttpsUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIs("https") && !url.has_username() &&
         !url.has_password() && !url.has_ref();
}

void DeleteTemporaryFile(base::FilePath path) {
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

}  // namespace

UboSimpleUrlLoaderClient::UboSimpleUrlLoaderClient(Profile* profile)
    : profile_(profile) {}

UboSimpleUrlLoaderClient::~UboSimpleUrlLoaderClient() {
  Cancel();
}

bool UboSimpleUrlLoaderClient::Begin(const GURL& exact_url, RequestKind kind) {
  if (loader_ || !profile_ || profile_->IsOffTheRecord() ||
      !IsExactHttpsUrl(exact_url)) {
    return false;
  }
  expected_url_ = exact_url;
  kind_ = kind;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = exact_url;
  request->method = "GET";
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader_->SetTimeoutDuration(kRequestTimeout);
  loader_->SetOnRedirectCallback(base::BindRepeating(
      &UboSimpleUrlLoaderClient::OnRedirect, weak_factory_.GetWeakPtr()));
  return true;
}

void UboSimpleUrlLoaderClient::FetchCatalog(
    const GURL& exact_url,
    UboCatalogDownloadCallback callback) {
  if (!callback) {
    return;
  }
  if (!Begin(exact_url, RequestKind::kCatalog)) {
    std::move(callback).Run(base::unexpected(UboNetworkError::kInvalidRequest));
    return;
  }
  catalog_callback_ = std::move(callback);
  loader_->DownloadToString(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&UboSimpleUrlLoaderClient::OnCatalogComplete,
                     weak_factory_.GetWeakPtr()),
      kMaximumUboCatalogBytes);
}

void UboSimpleUrlLoaderClient::FetchPackage(
    const GURL& exact_url,
    UboDownloadProgressCallback progress,
    UboPackageDownloadCallback callback) {
  if (!callback) {
    return;
  }
  if (!Begin(exact_url, RequestKind::kPackage)) {
    std::move(callback).Run(base::unexpected(UboNetworkError::kInvalidRequest));
    return;
  }
  package_callback_ = std::move(callback);
  if (progress) {
    loader_->SetOnDownloadProgressCallback(std::move(progress));
  }
  loader_->DownloadToTempFile(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&UboSimpleUrlLoaderClient::OnPackageComplete,
                     weak_factory_.GetWeakPtr()),
      kMaximumUboPackageBytes);
}

void UboSimpleUrlLoaderClient::OnRedirect(
    const GURL& before,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* removed_headers) {
  const bool allowed_official_release_redirect =
      kind_ == RequestKind::kPackage && redirect_info.status_code == 302 &&
      redirect_info.new_method == "GET" &&
      IsAllowedUboPackageRedirect(expected_url_, before, redirect_info.new_url);
  if (allowed_official_release_redirect) {
    // Credentials are already omitted on the request. Explicit stripping keeps
    // that invariant visible at the one redirect boundary as well.
    if (removed_headers) {
      removed_headers->push_back(net::HttpRequestHeaders::kAuthorization);
      removed_headers->push_back(net::HttpRequestHeaders::kCookie);
    }
    return;
  }

  loader_.reset();
  if (kind_ == RequestKind::kCatalog && catalog_callback_) {
    std::move(catalog_callback_)
        .Run(base::unexpected(UboNetworkError::kRedirect));
  } else if (package_callback_) {
    std::move(package_callback_)
        .Run(base::unexpected(UboNetworkError::kRedirect));
  }
}

UboNetworkError UboSimpleUrlLoaderClient::ClassifyFailure() const {
  if (!loader_) {
    return UboNetworkError::kCancelled;
  }
  if (loader_->NetError() == net::ERR_INSUFFICIENT_RESOURCES) {
    return UboNetworkError::kResponseTooLarge;
  }
  if (loader_->NetError() == net::ERR_INTERNET_DISCONNECTED ||
      loader_->NetError() == net::ERR_NETWORK_CHANGED ||
      loader_->NetError() == net::ERR_NAME_NOT_RESOLVED ||
      loader_->NetError() == net::ERR_TIMED_OUT ||
      loader_->NetError() == net::ERR_CONNECTION_TIMED_OUT) {
    return UboNetworkError::kOffline;
  }
  return UboNetworkError::kUnexpectedResponse;
}

void UboSimpleUrlLoaderClient::OnCatalogComplete(
    std::optional<std::string> body) {
  UboCatalogDownloadCallback callback = std::move(catalog_callback_);
  if (!callback) {
    loader_.reset();
    return;
  }
  if (!body || loader_->NetError() != net::OK ||
      loader_->GetFinalURL() != expected_url_) {
    UboNetworkError error = ClassifyFailure();
    loader_.reset();
    std::move(callback).Run(base::unexpected(error));
    return;
  }
  GURL final_url = loader_->GetFinalURL();
  loader_.reset();
  std::move(callback).Run(UboCatalogDownload{final_url, std::move(*body)});
}

void UboSimpleUrlLoaderClient::OnPackageComplete(base::FilePath path) {
  UboPackageDownloadCallback callback = std::move(package_callback_);
  if (!callback) {
    loader_.reset();
    return;
  }
  if (path.empty() || loader_->NetError() != net::OK ||
      !IsAllowedUboPackageFinalUrl(expected_url_, loader_->GetFinalURL())) {
    UboNetworkError error = ClassifyFailure();
    loader_.reset();
    if (!path.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&DeleteTemporaryFile, std::move(path)));
    }
    std::move(callback).Run(base::unexpected(error));
    return;
  }
  GURL final_url = loader_->GetFinalURL();
  loader_.reset();
  std::move(callback).Run(UboPackageDownload{final_url, std::move(path)});
}

void UboSimpleUrlLoaderClient::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  loader_.reset();
  if (catalog_callback_) {
    std::move(catalog_callback_)
        .Run(base::unexpected(UboNetworkError::kCancelled));
  }
  if (package_callback_) {
    std::move(package_callback_)
        .Run(base::unexpected(UboNetworkError::kCancelled));
  }
}

}  // namespace ahoi::extensions
