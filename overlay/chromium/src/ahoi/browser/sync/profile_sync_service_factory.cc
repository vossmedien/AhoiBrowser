// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service_factory.h"

#include <memory>

#include "ahoi/browser/sync/profile_sync_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace ahoi::sync {

ProfileSyncService* ProfileSyncServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<ProfileSyncService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

ProfileSyncServiceFactory* ProfileSyncServiceFactory::GetInstance() {
  static base::NoDestructor<ProfileSyncServiceFactory> instance;
  return instance.get();
}

ProfileSyncServiceFactory::ProfileSyncServiceFactory()
    : ProfileKeyedServiceFactory("AhoiProfileSyncService",
                                 ProfileSelections::BuildForRegularProfile()) {
  DependsOn(HistoryServiceFactory::GetInstance());
}

ProfileSyncServiceFactory::~ProfileSyncServiceFactory() = default;

std::unique_ptr<KeyedService>
ProfileSyncServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* const profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      !profile->AllowsBrowserWindows()) {
    return nullptr;
  }
  return std::make_unique<ProfileSyncService>(profile);
}

bool ProfileSyncServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ahoi::sync
