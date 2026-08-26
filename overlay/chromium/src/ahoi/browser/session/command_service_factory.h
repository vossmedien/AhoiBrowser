// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_COMMAND_SERVICE_FACTORY_H_
#define AHOI_BROWSER_SESSION_COMMAND_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi {

class CommandService;

// Owns one CommandService for each regular, non-OTR Profile. Incognito,
// guest, system and Ash-internal profiles deliberately receive no instance.
class CommandServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static CommandService* GetForProfile(Profile* profile);
  static CommandServiceFactory* GetInstance();

  CommandServiceFactory(const CommandServiceFactory&) = delete;
  CommandServiceFactory& operator=(const CommandServiceFactory&) = delete;

 private:
  friend base::NoDestructor<CommandServiceFactory>;

  CommandServiceFactory();
  ~CommandServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_SESSION_COMMAND_SERVICE_FACTORY_H_
