// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_service_factory.h"

#include <memory>

#include "ahoi/browser/extensions/ubo_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"

namespace ahoi::extensions {

// static
UboService* UboServiceFactory::GetForProfile(Profile* profile) {
  if (!profile || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return static_cast<UboService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
UboServiceFactory* UboServiceFactory::GetInstance() {
  static base::NoDestructor<UboServiceFactory> instance;
  return instance.get();
}

UboServiceFactory::UboServiceFactory()
    : ProfileKeyedServiceFactory("AhoiUboService",
                                 ProfileSelections::BuildForRegularProfile()) {}

UboServiceFactory::~UboServiceFactory() = default;

std::unique_ptr<KeyedService>
UboServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return std::make_unique<UboService>(profile);
}

bool UboServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ahoi::extensions
