// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SESSION_CONTROLLER_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SESSION_CONTROLLER_H_

#include <optional>
#include <string_view>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
}

namespace ahoi {

// Owns only the active tab's volatile HTTP-auth identity. It never stores a
// password. The state dies with WebContents, which makes OTR state strictly
// transient and prevents it from leaking into history, sync, or CloudKit.
class HttpAuthSessionController final
    : public content::WebContentsObserver,
      public content::WebContentsUserData<HttpAuthSessionController> {
 public:
  HttpAuthSessionController(const HttpAuthSessionController&) = delete;
  HttpAuthSessionController& operator=(const HttpAuthSessionController&) =
      delete;
  ~HttpAuthSessionController() override;

  static HttpAuthSessionController* GetOrCreate(
      content::WebContents* web_contents);

  void RecordSuccessfulAuthentication(HttpAuthProtectionSpace protection_space);
  const std::optional<HttpAuthProtectionSpace>& active_protection_space()
      const {
    return active_protection_space_;
  }

  // Clears only the active origin's HTTP-auth cache, then closes the profile
  // NetworkContext's live connections before reloading. Saved PasswordStore
  // credentials are intentionally retained.
  void SwitchAccount(base::OnceClosure done = base::DoNothing());

  // Removes one saved account without changing the current auth session.
  void DeleteSavedCredential(std::u16string_view username,
                             base::OnceClosure done = base::DoNothing());

  // Deletes saved accounts for the current realm, then performs the same
  // narrow auth-cache/connection/reload sequence as SwitchAccount().
  void ForgetRealmAndSwitch(base::OnceClosure done = base::DoNothing());

 private:
  friend class content::WebContentsUserData<HttpAuthSessionController>;

  explicit HttpAuthSessionController(content::WebContents* web_contents);

  void ClearCacheAndConnections(base::OnceClosure done);
  void OnAuthCacheCleared(base::OnceClosure done);
  void OnConnectionsClosed(base::OnceClosure done);
  HttpAuthRequestContext request_context() const;

  std::optional<HttpAuthProtectionSpace> active_protection_space_;
  base::WeakPtrFactory<HttpAuthSessionController> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_SESSION_CONTROLLER_H_
