// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/session_bridge_factory.h"

#include <memory>

#include "ahoi/browser/session/command_service_factory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_manager_service_factory.h"

namespace ahoi {

// static
SessionBridge* SessionBridgeFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<SessionBridge*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
SessionBridgeFactory* SessionBridgeFactory::GetInstance() {
  static base::NoDestructor<SessionBridgeFactory> instance;
  return instance.get();
}

SessionBridgeFactory::SessionBridgeFactory()
    : ProfileKeyedServiceFactory("AhoiSessionBridge",
                                 ProfileSelections::BuildForRegularProfile()) {
  DependsOn(BrowserManagerServiceFactory::GetInstance());
  DependsOn(CommandServiceFactory::GetInstance());
  DependsOn(WorkspaceServiceFactory::GetInstance());
}

SessionBridgeFactory::~SessionBridgeFactory() = default;

std::unique_ptr<KeyedService>
SessionBridgeFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      !profile->AllowsBrowserWindows()) {
    return nullptr;
  }
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(profile);
  CommandService* command_service =
      CommandServiceFactory::GetForProfile(profile);
  if (!workspace_service || !command_service) {
    return nullptr;
  }
  return std::make_unique<SessionBridge>(profile, workspace_service,
                                         command_service);
}

bool SessionBridgeFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ahoi
