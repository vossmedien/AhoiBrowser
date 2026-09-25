// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_
#define AHOI_BROWSER_SYNC_PROFILE_SYNC_SERVICE_FACTORY_H_

#include <memory>
#include <optional>
#include <string_view>

#include "ahoi/browser/sync/sync_namespace.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class PrefService;
class Profile;

namespace ahoi::sync {

class ProfileSyncService;

class ProfileSyncServiceFactory final : public ProfileKeyedServiceFactory {
 public:
  static ProfileSyncService* GetForProfile(Profile* profile);
  static ProfileSyncServiceFactory* GetInstance();

  // ADR 0011 step 4: the namespace of the Profile in `profile_dir`, read from
  // the Local State registry of fully separated Workspaces. An unlisted
  // Profile syncs in the main namespace; a listed one only in its Workspace's
  // namespace. Nullopt (no sync service) for a Profile being deleted or whose
  // registry entry is present but unreadable, so it can never fall back to
  // the main zone.
  static std::optional<SyncNamespace> SyncNamespaceForProfileDir(
      const PrefService* local_state,
      std::string_view profile_dir);

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
