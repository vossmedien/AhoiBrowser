// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/secure_component_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "components/update_client/network.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

namespace ahoi::privacy {
namespace {

class SecureComponentNetworkFetcher final
    : public update_client::NetworkFetcher {
 public:
  explicit SecureComponentNetworkFetcher(
      std::unique_ptr<update_client::NetworkFetcher> delegate)
      : delegate_(std::move(delegate)) {
    CHECK(delegate_);
  }

  void PostRequest(
      const GURL& url,
      const std::string& post_data,
      const std::string& content_type,
      const base::flat_map<std::string, std::string>& post_additional_headers,
      ResponseStartedCallback response_started_callback,
      ProgressCallback progress_callback,
      PostRequestCompleteCallback post_request_complete_callback) override {
    if (IsSecureComponentTransportUrl(url)) {
      delegate_->PostRequest(
          url, post_data, content_type, post_additional_headers,
          std::move(response_started_callback), std::move(progress_callback),
          std::move(post_request_complete_callback));
      return;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(post_request_complete_callback), std::nullopt,
                       net::ERR_DISALLOWED_URL_SCHEME, std::string(),
                       std::string(), std::string(), -1));
  }

  base::OnceClosure DownloadToFile(
      const GURL& url,
      const base::FilePath& file_path,
      ResponseStartedCallback response_started_callback,
      ProgressCallback progress_callback,
      DownloadToFileCompleteCallback download_to_file_complete_callback)
      override {
    if (IsSecureComponentTransportUrl(url)) {
      return delegate_->DownloadToFile(
          url, file_path, std::move(response_started_callback),
          std::move(progress_callback),
          std::move(download_to_file_complete_callback));
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(download_to_file_complete_callback),
                                  net::ERR_DISALLOWED_URL_SCHEME, 0));
    return base::DoNothing();
  }

 private:
  std::unique_ptr<update_client::NetworkFetcher> delegate_;
};

class SecureComponentNetworkFetcherFactory final
    : public update_client::NetworkFetcherFactory {
 public:
  explicit SecureComponentNetworkFetcherFactory(
      scoped_refptr<update_client::NetworkFetcherFactory> delegate)
      : delegate_(std::move(delegate)) {
    CHECK(delegate_);
  }

  std::unique_ptr<update_client::NetworkFetcher> Create() const override {
    return std::make_unique<SecureComponentNetworkFetcher>(delegate_->Create());
  }

 private:
  ~SecureComponentNetworkFetcherFactory() override = default;

  scoped_refptr<update_client::NetworkFetcherFactory> delegate_;
};

}  // namespace

bool IsSecureComponentTransportUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsCryptographic();
}

scoped_refptr<update_client::NetworkFetcherFactory>
WrapSecureComponentNetworkFetcherFactory(
    scoped_refptr<update_client::NetworkFetcherFactory> delegate) {
  return base::MakeRefCounted<SecureComponentNetworkFetcherFactory>(
      std::move(delegate));
}

}  // namespace ahoi::privacy
