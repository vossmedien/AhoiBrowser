// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace ahoi::sync {

class ProfileSyncService;

class ProfileSyncServiceFactory final : public ProfileKeyedServiceFactory {
 public:
  static ProfileSyncService* GetForProfile(Profile* profile);
  static ProfileSyncServiceFactory* GetInstance();

  ProfileSyncServiceFactory(const ProfileSyncServiceFactory&) = delete;
  ProfileSyncServiceFactory& operator=(const ProfileSyncServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<ProfileSyncServiceFactory>;

  ProfileSyncServiceFactory();
  ~ProfileSyncServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace ahoi::sync

#endif  // AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_
