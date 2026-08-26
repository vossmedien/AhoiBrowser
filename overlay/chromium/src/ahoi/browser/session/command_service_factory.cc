// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/command_service_factory.h"

#include <memory>

#include "ahoi/browser/navigation/command_service.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi {

// static
CommandService* CommandServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<CommandService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
CommandServiceFactory* CommandServiceFactory::GetInstance() {
  static base::NoDestructor<CommandServiceFactory> instance;
  return instance.get();
}

CommandServiceFactory::CommandServiceFactory()
    : ProfileKeyedServiceFactory("AhoiCommandService",
                                 ProfileSelections::BuildForRegularProfile()) {}

CommandServiceFactory::~CommandServiceFactory() = default;

std::unique_ptr<KeyedService>
CommandServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return std::make_unique<CommandService>();
}

}  // namespace ahoi
