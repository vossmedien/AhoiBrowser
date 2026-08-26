// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_ACCESS_CONTROLLER_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_ACCESS_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace device_reauth {
class DeviceAuthenticator;
}

namespace ahoi {

// Coordinates system reauthentication before any saved HTTP-auth secret is
// fetched or a sensitive editor action is authorized. The injected callbacks
// keep WebContents and PasswordStore concerns outside this class and provide a
// deterministic seam for cancellation/race tests.
class HttpAuthSecretAccessController {
 public:
  using ContextIsValidCallback = base::RepeatingCallback<bool()>;
  using SecretResultCallback =
      base::OnceCallback<void(std::optional<std::u16string>)>;
  using SecretLoaderCallback =
      base::RepeatingCallback<void(const HttpAuthCredentialMetadata&,
                                   SecretResultCallback)>;
  using AuthorizationCallback = base::OnceCallback<void(bool)>;

  HttpAuthSecretAccessController(
      std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
      ContextIsValidCallback context_is_valid,
      SecretLoaderCallback secret_loader);
  HttpAuthSecretAccessController(const HttpAuthSecretAccessController&) =
      delete;
  HttpAuthSecretAccessController& operator=(
      const HttpAuthSecretAccessController&) = delete;
  ~HttpAuthSecretAccessController();

  // Runs a fresh system authentication. There is deliberately no cached
  // authorization state in this controller.
  void Authorize(std::u16string prompt, AuthorizationCallback callback);

  // Authenticates first and only then invokes the injected secret loader.
  void RequestSecret(HttpAuthCredentialMetadata metadata,
                     std::u16string prompt,
                     SecretResultCallback callback);

  // Cancels an authentication in progress and makes every outstanding auth or
  // secret-loader callback stale. Stale callbacks never reach the caller.
  void Invalidate();

 private:
  void OnAuthorizationFinished(uint64_t generation,
                               AuthorizationCallback callback,
                               bool authenticated);
  void OnSecretAuthorizationFinished(HttpAuthCredentialMetadata metadata,
                                     SecretResultCallback callback,
                                     bool authenticated);
  void OnSecretLoaded(uint64_t generation,
                      SecretResultCallback callback,
                      std::optional<std::u16string> secret);
  bool IsCurrentAndValid(uint64_t generation) const;

  std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator_;
  ContextIsValidCallback context_is_valid_;
  SecretLoaderCallback secret_loader_;
  uint64_t generation_ = 0;
  bool authentication_pending_ = false;
  base::WeakPtrFactory<HttpAuthSecretAccessController> weak_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SECRET_ACCESS_CONTROLLER_H_
