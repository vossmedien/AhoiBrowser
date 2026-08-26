// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_secret_access_controller.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/http_auth/http_auth_secret_util.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "components/device_reauth/device_authenticator.h"
#include "components/device_reauth/mock_device_authenticator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::StrictMock;

HttpAuthCredentialMetadata MakeMetadata() {
  HttpAuthCredentialMetadata metadata;
  metadata.username = u"alice";
  return metadata;
}

class HttpAuthSecretAccessControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto authenticator =
        std::make_unique<StrictMock<device_reauth::MockDeviceAuthenticator>>();
    authenticator_ = authenticator.get();
    controller_ = std::make_unique<HttpAuthSecretAccessController>(
        std::move(authenticator),
        base::BindRepeating(&HttpAuthSecretAccessControllerTest::IsContextValid,
                            base::Unretained(this)),
        base::BindRepeating(&HttpAuthSecretAccessControllerTest::LoadSecret,
                            base::Unretained(this)));
  }

  bool IsContextValid() const { return context_is_valid_; }

  void LoadSecret(
      const HttpAuthCredentialMetadata& metadata,
      HttpAuthSecretAccessController::SecretResultCallback callback) {
    ++loader_call_count_;
    loaded_username_ = metadata.username;
    if (hold_loader_callback_) {
      loader_callback_ = std::move(callback);
      return;
    }
    std::move(callback).Run(std::u16string(u"stored-secret"));
  }

 protected:
  raw_ptr<StrictMock<device_reauth::MockDeviceAuthenticator>> authenticator_ =
      nullptr;
  std::unique_ptr<HttpAuthSecretAccessController> controller_;
  bool context_is_valid_ = true;
  bool hold_loader_callback_ = false;
  int loader_call_count_ = 0;
  std::u16string loaded_username_;
  HttpAuthSecretAccessController::SecretResultCallback loader_callback_;
};

TEST_F(HttpAuthSecretAccessControllerTest,
       FetchesSecretOnlyAfterSuccessfulAuthentication) {
  device_reauth::DeviceAuthenticator::AuthenticateCallback auth_callback;
  EXPECT_CALL(*authenticator_,
              AuthenticateWithMessage(Eq(std::u16string(u"Authenticate")), _))
      .WillOnce([&auth_callback](
                    const std::u16string&,
                    device_reauth::DeviceAuthenticator::AuthenticateCallback
                        callback) { auth_callback = std::move(callback); });

  std::optional<std::u16string> result;
  bool result_called = false;
  controller_->RequestSecret(
      MakeMetadata(), u"Authenticate",
      base::BindOnce(
          [](bool* called, std::optional<std::u16string>* result,
             std::optional<std::u16string> secret) {
            *called = true;
            *result = std::move(secret);
          },
          &result_called, &result));

  EXPECT_EQ(0, loader_call_count_);
  ASSERT_TRUE(auth_callback);
  std::move(auth_callback).Run(true);

  EXPECT_TRUE(result_called);
  ASSERT_TRUE(result);
  EXPECT_EQ(u"stored-secret", *result);
  EXPECT_EQ(u"alice", loaded_username_);
}

TEST_F(HttpAuthSecretAccessControllerTest,
       AuthenticationFailureNeverInvokesSecretLoader) {
  device_reauth::DeviceAuthenticator::AuthenticateCallback auth_callback;
  EXPECT_CALL(*authenticator_, AuthenticateWithMessage(_, _))
      .WillOnce([&auth_callback](
                    const std::u16string&,
                    device_reauth::DeviceAuthenticator::AuthenticateCallback
                        callback) { auth_callback = std::move(callback); });

  bool result_called = false;
  std::optional<std::u16string> result = u"unexpected";
  controller_->RequestSecret(
      MakeMetadata(), u"Authenticate",
      base::BindOnce(
          [](bool* called, std::optional<std::u16string>* result,
             std::optional<std::u16string> secret) {
            *called = true;
            *result = std::move(secret);
          },
          &result_called, &result));
  std::move(auth_callback).Run(false);

  EXPECT_TRUE(result_called);
  EXPECT_FALSE(result);
  EXPECT_EQ(0, loader_call_count_);
}

TEST_F(HttpAuthSecretAccessControllerTest,
       MissingOrInvalidContextFailsClosedBeforeAuthentication) {
  context_is_valid_ = false;
  bool authorized = true;
  controller_->Authorize(
      u"Authenticate",
      base::BindOnce([](bool* result, bool success) { *result = success; },
                     &authorized));
  EXPECT_FALSE(authorized);
  EXPECT_EQ(0, loader_call_count_);
}

TEST_F(HttpAuthSecretAccessControllerTest,
       InvalidationCancelsAuthenticationAndIgnoresStaleCallback) {
  device_reauth::DeviceAuthenticator::AuthenticateCallback auth_callback;
  EXPECT_CALL(*authenticator_, AuthenticateWithMessage(_, _))
      .WillOnce([&auth_callback](
                    const std::u16string&,
                    device_reauth::DeviceAuthenticator::AuthenticateCallback
                        callback) { auth_callback = std::move(callback); });
  EXPECT_CALL(*authenticator_, Cancel());

  bool result_called = false;
  controller_->RequestSecret(
      MakeMetadata(), u"Authenticate",
      base::BindOnce(
          [](bool* called, std::optional<std::u16string>) { *called = true; },
          &result_called));
  controller_->Invalidate();
  std::move(auth_callback).Run(true);

  EXPECT_FALSE(result_called);
  EXPECT_EQ(0, loader_call_count_);
}

TEST_F(HttpAuthSecretAccessControllerTest,
       InvalidationIgnoresSecretArrivingAfterContextBecameStale) {
  hold_loader_callback_ = true;
  device_reauth::DeviceAuthenticator::AuthenticateCallback auth_callback;
  EXPECT_CALL(*authenticator_, AuthenticateWithMessage(_, _))
      .WillOnce([&auth_callback](
                    const std::u16string&,
                    device_reauth::DeviceAuthenticator::AuthenticateCallback
                        callback) { auth_callback = std::move(callback); });

  bool result_called = false;
  controller_->RequestSecret(
      MakeMetadata(), u"Authenticate",
      base::BindOnce(
          [](bool* called, std::optional<std::u16string>) { *called = true; },
          &result_called));
  std::move(auth_callback).Run(true);
  ASSERT_TRUE(loader_callback_);
  context_is_valid_ = false;
  controller_->Invalidate();
  std::move(loader_callback_).Run(std::u16string(u"late-secret"));

  EXPECT_FALSE(result_called);
}

TEST(HttpAuthSecretAccessControllerStandaloneTest,
     MissingAuthenticatorFailsClosed) {
  bool context_valid = true;
  int loader_call_count = 0;
  HttpAuthSecretAccessController controller(
      nullptr,
      base::BindRepeating([](bool* valid) { return *valid; }, &context_valid),
      base::BindRepeating(
          [](int* calls, const HttpAuthCredentialMetadata&,
             HttpAuthSecretAccessController::SecretResultCallback callback) {
            ++*calls;
            std::move(callback).Run(std::u16string(u"unexpected"));
          },
          &loader_call_count));

  bool authorized = true;
  controller.Authorize(
      u"Authenticate",
      base::BindOnce([](bool* result, bool success) { *result = success; },
                     &authorized));
  EXPECT_FALSE(authorized);
  EXPECT_EQ(0, loader_call_count);
}

TEST(HttpAuthSecretUtilTest, ExplicitClearLeavesNoLogicalPlaintext) {
  std::u16string secret = u"transient-secret";
  SecurelyClearHttpAuthSecret(&secret);
  EXPECT_TRUE(secret.empty());
}

}  // namespace
}  // namespace ahoi
