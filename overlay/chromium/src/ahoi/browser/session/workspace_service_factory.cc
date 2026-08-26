// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_service_factory.h"

#include <memory>

#include "ahoi/browser/navigation/workspace_service.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi {

// static
WorkspaceService* WorkspaceServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<WorkspaceService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
WorkspaceServiceFactory* WorkspaceServiceFactory::GetInstance() {
  static base::NoDestructor<WorkspaceServiceFactory> instance;
  return instance.get();
}

WorkspaceServiceFactory::WorkspaceServiceFactory()
    : ProfileKeyedServiceFactory("AhoiWorkspaceService",
                                 ProfileSelections::BuildForRegularProfile()) {}

WorkspaceServiceFactory::~WorkspaceServiceFactory() = default;

std::unique_ptr<KeyedService>
WorkspaceServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile()) {
    return nullptr;
  }
  return std::make_unique<WorkspaceService>();
}

}  // namespace ahoi
