// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_FACTORY_H_
#define AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi {

class HttpAuthCredentialService;

// Owns one HTTP-auth credential service for each regular profile. The
// ProfileSelections policy redirects an OTR BrowserContext to its regular
// profile only for service ownership; callers still have to pass
// HttpAuthRequestContext::kIncognito, which makes OTR reads explicit and all
// OTR writes fail closed.
class HttpAuthCredentialServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static HttpAuthCredentialService* GetForProfile(Profile* profile);
  static HttpAuthCredentialServiceFactory* GetInstance();

  HttpAuthCredentialServiceFactory(const HttpAuthCredentialServiceFactory&) =
      delete;
  HttpAuthCredentialServiceFactory& operator=(
      const HttpAuthCredentialServiceFactory&) = delete;

 private:
  friend base::NoDestructor<HttpAuthCredentialServiceFactory>;

  HttpAuthCredentialServiceFactory();
  ~HttpAuthCredentialServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_HTTP_AUTH_HTTP_AUTH_CREDENTIAL_SERVICE_FACTORY_H_
