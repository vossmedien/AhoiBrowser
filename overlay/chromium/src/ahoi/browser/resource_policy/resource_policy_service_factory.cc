// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/resource_policy/resource_policy_service_factory.h"

#include <memory>

#include "ahoi/browser/resource_policy/resource_policy_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"

namespace ahoi::resource_policy {

// static
ResourcePolicyService* ResourcePolicyServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return static_cast<ResourcePolicyService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
ResourcePolicyServiceFactory* ResourcePolicyServiceFactory::GetInstance() {
  static base::NoDestructor<ResourcePolicyServiceFactory> instance;
  return instance.get();
}

ResourcePolicyServiceFactory::ResourcePolicyServiceFactory()
    : ProfileKeyedServiceFactory("AhoiResourcePolicyService",
                                 ProfileSelections::BuildForRegularProfile()) {}

ResourcePolicyServiceFactory::~ResourcePolicyServiceFactory() = default;

std::unique_ptr<KeyedService>
ResourcePolicyServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return std::make_unique<ResourcePolicyService>(profile);
}

bool ResourcePolicyServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ahoi::resource_policy
