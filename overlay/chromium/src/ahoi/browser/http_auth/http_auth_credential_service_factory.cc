// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/http_auth/http_auth_credential_service_factory.h"

#include <memory>

#include "ahoi/browser/http_auth/http_auth_credential_service.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/keyed_service/core/service_access_type.h"

namespace ahoi {

// static
HttpAuthCredentialService* HttpAuthCredentialServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<HttpAuthCredentialService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
HttpAuthCredentialServiceFactory*
HttpAuthCredentialServiceFactory::GetInstance() {
  static base::NoDestructor<HttpAuthCredentialServiceFactory> instance;
  return instance.get();
}

HttpAuthCredentialServiceFactory::HttpAuthCredentialServiceFactory()
    : ProfileKeyedServiceFactory(
          "AhoiHttpAuthCredentialService",
          ProfileSelections::BuildRedirectedInIncognito()) {
  DependsOn(ProfilePasswordStoreFactory::GetInstance());
}

HttpAuthCredentialServiceFactory::~HttpAuthCredentialServiceFactory() = default;

std::unique_ptr<KeyedService>
HttpAuthCredentialServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || !profile->IsRegularProfile()) {
    return nullptr;
  }

  scoped_refptr<password_manager::PasswordStoreInterface> password_store =
      ProfilePasswordStoreFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!password_store) {
    return nullptr;
  }
  return std::make_unique<HttpAuthCredentialService>(profile, password_store);
}

}  // namespace ahoi
