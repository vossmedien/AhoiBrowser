// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_FACTORY_H_
#define AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi::resource_policy {

class ResourcePolicyService;

class ResourcePolicyServiceFactory final : public ProfileKeyedServiceFactory {
 public:
  static ResourcePolicyService* GetForProfile(Profile* profile);
  static ResourcePolicyServiceFactory* GetInstance();

 private:
  friend base::NoDestructor<ResourcePolicyServiceFactory>;

  ResourcePolicyServiceFactory();
  ~ResourcePolicyServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace ahoi::resource_policy

#endif  // AHOI_BROWSER_RESOURCE_POLICY_RESOURCE_POLICY_SERVICE_FACTORY_H_
