// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_FACTORY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi::importer::arc {

class ArcImportService;

class ArcImportServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ArcImportService* GetForProfile(Profile* profile);
  static ArcImportServiceFactory* GetInstance();

  ArcImportServiceFactory(const ArcImportServiceFactory&) = delete;
  ArcImportServiceFactory& operator=(const ArcImportServiceFactory&) = delete;

 private:
  friend base::NoDestructor<ArcImportServiceFactory>;

  ArcImportServiceFactory();
  ~ArcImportServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SERVICE_FACTORY_H_
