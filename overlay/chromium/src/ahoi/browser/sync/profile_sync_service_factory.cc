// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/sync/profile_sync_service_factory.h"

#include <memory>
#include <string>
#include <utility>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/sync/profile_sync_service.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extensions_browser_client.h"

namespace ahoi::sync {

ProfileSyncService* ProfileSyncServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<ProfileSyncService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
std::optional<SyncNamespace>
ProfileSyncServiceFactory::SyncNamespaceForProfileDir(
    const PrefService* local_state,
    std::string_view profile_dir) {
  if (!local_state) {
    return SyncNamespace::Main();
  }
  const std::optional<session::IsolatedProfileEntry> entry =
      session::FindIsolatedProfile(local_state, profile_dir);
  if (!entry) {
    // The registry skips malformed entries. A malformed entry naming this
    // directory still marks a separated Profile: fail closed.
    if (local_state->FindPreference(session::kIsolatedProfilesPref)) {
      for (const base::Value& value :
           local_state->GetList(session::kIsolatedProfilesPref)) {
        const base::DictValue* dict = value.GetIfDict();
        const std::string* dir =
            dict ? dict->FindString("profile_dir") : nullptr;
        if (dir && *dir == profile_dir) {
          return std::nullopt;
        }
      }
    }
    return SyncNamespace::Main();
  }
  if (entry->state == session::IsolatedProfileState::kDeleting) {
    return std::nullopt;
  }
  return SyncNamespace::ForSeparatedWorkspace(entry->workspace_id);
}

ProfileSyncServiceFactory* ProfileSyncServiceFactory::GetInstance() {
  static base::NoDestructor<ProfileSyncServiceFactory> instance;
  return instance.get();
}

ProfileSyncServiceFactory::ProfileSyncServiceFactory()
    : ProfileKeyedServiceFactory("AhoiProfileSyncService",
                                 ProfileSelections::BuildForRegularProfile()) {
  DependsOn(HistoryServiceFactory::GetInstance());
  DependsOn(BookmarkModelFactory::GetInstance());
  DependsOn(BookmarkMergedSurfaceServiceFactory::GetInstance());
  DependsOn(TemplateURLServiceFactory::GetInstance());
  DependsOn(extensions::StorageFrontend::GetFactoryInstance());
  DependsOn(extensions::ExtensionManagementFactory::GetInstance());
  DependsOn(
      extensions::ExtensionsBrowserClient::Get()->GetExtensionSystemFactory());
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
  // ADR 0011 step 4: a fully separated Workspace's Profile syncs only in its
  // own CloudKit zone with its own key. Sync stays opt-in per Profile
  // (kSyncEnabledPref defaults to false), so a new separated Profile starts
  // with no transport until the user enables it there.
  std::optional<SyncNamespace> sync_namespace =
      SyncNamespaceForProfileDir(
          g_browser_process ? g_browser_process->local_state() : nullptr,
          profile->GetPath().BaseName().AsUTF8Unsafe());
  if (!sync_namespace) {
    return nullptr;
  }
  return std::make_unique<ProfileSyncService>(profile,
                                              std::move(*sync_namespace));
}

bool ProfileSyncServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ahoi::sync
