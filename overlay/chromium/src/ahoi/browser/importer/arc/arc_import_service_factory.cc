// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_service_factory.h"

#include <memory>

#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi::importer::arc {

// static
ArcImportService* ArcImportServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<ArcImportService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
ArcImportServiceFactory* ArcImportServiceFactory::GetInstance() {
  static base::NoDestructor<ArcImportServiceFactory> instance;
  return instance.get();
}

ArcImportServiceFactory::ArcImportServiceFactory()
    : ProfileKeyedServiceFactory("AhoiArcImportService",
                                 ProfileSelections::BuildForRegularProfile()) {
  DependsOn(SessionBridgeFactory::GetInstance());
}

ArcImportServiceFactory::~ArcImportServiceFactory() = default;

std::unique_ptr<KeyedService>
ArcImportServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      !profile->AllowsBrowserWindows()) {
    return nullptr;
  }
  SessionBridge* session_bridge = SessionBridgeFactory::GetForProfile(profile);
  if (!session_bridge) {
    return nullptr;
  }
  return std::make_unique<ArcImportService>(profile, session_bridge);
}

}  // namespace ahoi::importer::arc
