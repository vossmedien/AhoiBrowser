// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_secret_access_controller.h"

#include <utility>

#include "ahoi/browser/http_auth/http_auth_secret_util.h"
#include "base/functional/bind.h"
#include "components/device_reauth/device_authenticator.h"

namespace ahoi {

HttpAuthSecretAccessController::HttpAuthSecretAccessController(
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    ContextIsValidCallback context_is_valid,
    SecretLoaderCallback secret_loader)
    : authenticator_(std::move(authenticator)),
      context_is_valid_(std::move(context_is_valid)),
      secret_loader_(std::move(secret_loader)) {}

HttpAuthSecretAccessController::~HttpAuthSecretAccessController() {
  Invalidate();
}

void HttpAuthSecretAccessController::Authorize(std::u16string prompt,
                                               AuthorizationCallback callback) {
  Invalidate();
  if (!callback) {
    return;
  }
  if (!authenticator_ || !context_is_valid_ || !context_is_valid_.Run()) {
    std::move(callback).Run(false);
    return;
  }

  authentication_pending_ = true;
  const uint64_t generation = generation_;
  authenticator_->AuthenticateWithMessage(
      prompt, base::BindOnce(
                  &HttpAuthSecretAccessController::OnAuthorizationFinished,
                  weak_factory_.GetWeakPtr(), generation, std::move(callback)));
}

void HttpAuthSecretAccessController::RequestSecret(
    HttpAuthCredentialMetadata metadata,
    std::u16string prompt,
    SecretResultCallback callback) {
  if (!callback) {
    return;
  }
  Authorize(std::move(prompt),
            base::BindOnce(
                &HttpAuthSecretAccessController::OnSecretAuthorizationFinished,
                weak_factory_.GetWeakPtr(), std::move(metadata),
                std::move(callback)));
}

void HttpAuthSecretAccessController::Invalidate() {
  ++generation_;
  const bool cancel_authentication = authentication_pending_;
  authentication_pending_ = false;
  if (cancel_authentication && authenticator_) {
    authenticator_->Cancel();
  }
}

void HttpAuthSecretAccessController::OnAuthorizationFinished(
    uint64_t generation,
    AuthorizationCallback callback,
    bool authenticated) {
  if (generation != generation_) {
    return;
  }
  authentication_pending_ = false;
  if (!context_is_valid_ || !context_is_valid_.Run()) {
    return;
  }
  std::move(callback).Run(authenticated);
}

void HttpAuthSecretAccessController::OnSecretAuthorizationFinished(
    HttpAuthCredentialMetadata metadata,
    SecretResultCallback callback,
    bool authenticated) {
  if (!authenticated || !context_is_valid_ || !context_is_valid_.Run() ||
      !secret_loader_) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  const uint64_t generation = generation_;
  secret_loader_.Run(
      metadata, base::BindOnce(&HttpAuthSecretAccessController::OnSecretLoaded,
                               weak_factory_.GetWeakPtr(), generation,
                               std::move(callback)));
}

void HttpAuthSecretAccessController::OnSecretLoaded(
    uint64_t generation,
    SecretResultCallback callback,
    std::optional<std::u16string> secret) {
  if (!IsCurrentAndValid(generation)) {
    if (secret) {
      SecurelyClearHttpAuthSecret(&*secret);
    }
    return;
  }
  if (!secret || secret->empty()) {
    if (secret) {
      SecurelyClearHttpAuthSecret(&*secret);
    }
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(std::move(secret));
  if (secret) {
    SecurelyClearHttpAuthSecret(&*secret);
  }
}

bool HttpAuthSecretAccessController::IsCurrentAndValid(
    uint64_t generation) const {
  return generation == generation_ && context_is_valid_ &&
         context_is_valid_.Run();
}

}  // namespace ahoi
