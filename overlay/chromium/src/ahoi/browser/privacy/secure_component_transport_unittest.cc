// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/secure_component_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/test/task_environment.h"
#include "components/update_client/network.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::privacy {
namespace {

struct DelegateState {
  int post_requests = 0;
  int downloads = 0;
};

class TestNetworkFetcher final : public update_client::NetworkFetcher {
 public:
  explicit TestNetworkFetcher(DelegateState* state) : state_(state) {}

  void PostRequest(const GURL&,
                   const std::string&,
                   const std::string&,
                   const base::flat_map<std::string, std::string>&,
                   ResponseStartedCallback,
                   ProgressCallback,
                   PostRequestCompleteCallback callback) override {
    ++state_->post_requests;
    std::move(callback).Run(std::string("ok"), net::OK, std::string(),
                            std::string(), std::string(), -1);
  }

  base::OnceClosure DownloadToFile(
      const GURL&,
      const base::FilePath&,
      ResponseStartedCallback,
      ProgressCallback,
      DownloadToFileCompleteCallback callback) override {
    ++state_->downloads;
    std::move(callback).Run(net::OK, 7);
    return base::DoNothing();
  }

 private:
  raw_ptr<DelegateState> state_;
};

class TestNetworkFetcherFactory final
    : public update_client::NetworkFetcherFactory {
 public:
  explicit TestNetworkFetcherFactory(DelegateState* state) : state_(state) {}

  std::unique_ptr<update_client::NetworkFetcher> Create() const override {
    return std::make_unique<TestNetworkFetcher>(state_);
  }

 private:
  ~TestNetworkFetcherFactory() override = default;
  raw_ptr<DelegateState> state_;
};

TEST(SecureComponentTransportTest, AllowsOnlyCryptographicUrls) {
  EXPECT_TRUE(IsSecureComponentTransportUrl(GURL("https://example.test/a")));
  EXPECT_FALSE(IsSecureComponentTransportUrl(GURL("http://example.test/a")));
  EXPECT_FALSE(IsSecureComponentTransportUrl(GURL("file:///tmp/a")));
  EXPECT_FALSE(IsSecureComponentTransportUrl(GURL("not a url")));
}

TEST(SecureComponentTransportTest, RejectsPlaintextWithoutCallingDelegate) {
  base::test::TaskEnvironment task_environment;
  DelegateState state;
  auto factory = WrapSecureComponentNetworkFetcherFactory(
      base::MakeRefCounted<TestNetworkFetcherFactory>(&state));
  auto fetcher = factory->Create();
  int post_error = net::OK;
  int download_error = net::OK;

  fetcher->PostRequest(
      GURL("http://example.test/update"), "{}", "application/json", {}, {}, {},
      base::BindOnce([](int* output, std::optional<std::string>, int error,
                        const std::string&, const std::string&,
                        const std::string&, int64_t) { *output = error; },
                     &post_error));
  fetcher->DownloadToFile(
      GURL("http://example.test/component.crx"),
      base::FilePath(FILE_PATH_LITERAL("unused")), {}, {},
      base::BindOnce([](int* output, int error, int64_t) { *output = error; },
                     &download_error));
  task_environment.RunUntilIdle();

  EXPECT_EQ(net::ERR_DISALLOWED_URL_SCHEME, post_error);
  EXPECT_EQ(net::ERR_DISALLOWED_URL_SCHEME, download_error);
  EXPECT_EQ(0, state.post_requests);
  EXPECT_EQ(0, state.downloads);
}

TEST(SecureComponentTransportTest, DelegatesHttpsRequests) {
  DelegateState state;
  auto factory = WrapSecureComponentNetworkFetcherFactory(
      base::MakeRefCounted<TestNetworkFetcherFactory>(&state));
  auto fetcher = factory->Create();
  int post_error = net::ERR_FAILED;
  int download_error = net::ERR_FAILED;

  fetcher->PostRequest(
      GURL("https://example.test/update"), "{}", "application/json", {}, {}, {},
      base::BindOnce([](int* output, std::optional<std::string>, int error,
                        const std::string&, const std::string&,
                        const std::string&, int64_t) { *output = error; },
                     &post_error));
  fetcher->DownloadToFile(
      GURL("https://example.test/component.crx"),
      base::FilePath(FILE_PATH_LITERAL("unused")), {}, {},
      base::BindOnce([](int* output, int error, int64_t) { *output = error; },
                     &download_error));

  EXPECT_EQ(net::OK, post_error);
  EXPECT_EQ(net::OK, download_error);
  EXPECT_EQ(1, state.post_requests);
  EXPECT_EQ(1, state.downloads);
}

}  // namespace
}  // namespace ahoi::privacy
