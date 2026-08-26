// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_FACTORY_H_
#define AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi::extensions {

class UboService;

class UboServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static UboService* GetForProfile(Profile* profile);
  static UboServiceFactory* GetInstance();

  UboServiceFactory(const UboServiceFactory&) = delete;
  UboServiceFactory& operator=(const UboServiceFactory&) = delete;

 private:
  friend base::NoDestructor<UboServiceFactory>;

  UboServiceFactory();
  ~UboServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_EXTENSIONS_UBO_SERVICE_FACTORY_H_
