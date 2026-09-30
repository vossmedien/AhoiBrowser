// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/isolated_profile_creation.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/session/group_page_close.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/isolated_workspace_directory.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_init_params.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "components/prefs/pref_service.h"

namespace ahoi::session {

namespace {

std::string DirName(const base::FilePath& path) {
  return path.BaseName().AsUTF8Unsafe();
}

void OnIsolatedProfileInitialized(std::string dir,
                                  base::OnceCallback<void(bool)> done,
                                  Profile* profile) {
  if (!profile) {
    LOG(ERROR) << "Ahoi could not create a fully separated Workspace profile";
    if (PrefService* local_state = g_browser_process->local_state()) {
      RemoveIsolatedProfile(local_state, dir);
    }
    std::move(done).Run(false);
    return;
  }
  // `is_new_profile` stays false: true would open Chromium's first-run tabs.
  profiles::OpenBrowserWindowForProfile(
      base::BindOnce(
          [](base::OnceCallback<void(bool)> done,
             BrowserWindowInterface* browser) {
            std::move(done).Run(browser != nullptr);
          },
          std::move(done)),
      /*always_create=*/true, /*is_new_profile=*/false,
      /*open_command_line_urls=*/false, profile);
}

}  // namespace

namespace {

// Registers `entry` (its directory is chosen here) and creates the Profile
// and its window. Returns the directory, or nullopt when registration failed;
// `done` has then run with false.
std::optional<std::string> RegisterAndCreateIsolatedProfile(
    IsolatedProfileEntry entry,
    base::OnceCallback<void(bool)> done) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  PrefService* local_state = g_browser_process->local_state();
  if (!profile_manager || !local_state || entry.name.empty()) {
    std::move(done).Run(false);
    return std::nullopt;
  }
  ProfileAttributesStorage& storage =
      profile_manager->GetProfileAttributesStorage();
  // Same loop as ProfileManager::CreateMultiProfileAsync: the next directory
  // pref can be out of sync with the storage after a crash.
  base::FilePath path;
  do {
    path = profile_manager->GenerateNextProfileDirectoryPath();
  } while (storage.GetProfileAttributesWithPath(path));

  // Registered and committed before the Profile exists, so its SessionBridge
  // seeds this Workspace and a crash leaves only a sweepable entry.
  entry.profile_dir = DirName(path);
  if (!AddIsolatedProfile(local_state, entry)) {
    std::move(done).Run(false);
    return std::nullopt;
  }
  local_state->CommitPendingWrite();

  // Not ephemeral and not omitted: an ephemeral Profile is wiped at every
  // start. The Workspace name doubles as the Profile name.
  ProfileAttributesInitParams init_params;
  init_params.profile_path = path;
  init_params.profile_name = entry.name;
  init_params.icon_index = 0;
  storage.AddProfile(std::move(init_params));

  // As upstream does: clear any orphan directory at that path first.
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          &NukeProfileFromDisk, path,
          base::BindOnce(
              [](base::FilePath path, std::string dir,
                 base::OnceCallback<void(bool)> done) {
                ProfileManager* manager = g_browser_process->profile_manager();
                if (!manager) {
                  std::move(done).Run(false);
                  return;
                }
                manager->CreateProfileAsync(
                    path, base::BindOnce(&OnIsolatedProfileInitialized,
                                         std::move(dir), std::move(done)));
              },
              path, entry.profile_dir, std::move(done))));
  return entry.profile_dir;
}

// Conversions waiting for their new Profile's SessionBridge, by directory.
// In memory only: a restart interrupts the conversion, and the sweep or the
// Profile's own bridge then deletes the half-converted Profile.
std::map<std::string, PendingWorkspaceConversion>& PendingConversions() {
  static base::NoDestructor<std::map<std::string, PendingWorkspaceConversion>>
      pending;
  return *pending;
}

}  // namespace

void CreateIsolatedWorkspace(std::u16string name,
                             std::u16string icon,
                             std::optional<uint32_t> accent_argb,
                             std::string sort_key,
                             base::OnceCallback<void(bool)> done) {
  std::ignore = RegisterAndCreateIsolatedProfile(
      IsolatedProfileEntry{
          .workspace_id = base::Uuid::GenerateRandomV4(),
          .name = std::move(name),
          .icon = std::move(icon),
          .accent_argb = accent_argb,
          .state = IsolatedProfileState::kCreating,
          .sort_key = std::move(sort_key),
      },
      std::move(done));
}

void ConvertToIsolatedWorkspace(const IsolatedProfileEntry& presentation,
                                PendingWorkspaceConversion pending) {
  IsolatedProfileEntry entry = presentation;
  entry.profile_dir.clear();
  entry.state = IsolatedProfileState::kConverting;
  auto dir = std::make_shared<std::string>();
  const std::optional<std::string> created = RegisterAndCreateIsolatedProfile(
      std::move(entry),
      base::BindOnce(
          [](std::shared_ptr<std::string> dir, bool created) {
            if (created) {
              return;  // The new Profile's SessionBridge imports and reports.
            }
            if (std::optional<PendingWorkspaceConversion> failed =
                    TakePendingWorkspaceConversion(*dir)) {
              std::move(failed->done).Run(false);
            }
          },
          dir));
  if (!created) {
    std::move(pending.done).Run(false);
    return;
  }
  *dir = *created;
  PendingConversions().insert_or_assign(*created, std::move(pending));
}

IsolatedStructureCreation CreateIsolatedWorkspaceWithStructure(
    const IsolatedProfileEntry& presentation,
    PendingWorkspaceConversion pending) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (const std::optional<IsolatedProfileEntry> existing =
          FindIsolatedProfileByWorkspaceId(local_state,
                                           presentation.workspace_id)) {
    return existing->state == IsolatedProfileState::kDeleting
               ? IsolatedStructureCreation::kBeingDeleted
               : IsolatedStructureCreation::kAlreadyExists;
  }
  // The `converting` state and the pending import are exactly those of a
  // conversion; only the source side (which would delete its Workspace
  // after success) does not exist here.
  ConvertToIsolatedWorkspace(presentation, std::move(pending));
  return IsolatedStructureCreation::kStarted;
}

std::string NextIsolatedWorkspaceSortKey(
    const std::vector<DirectoryWorkspace>& main_workspaces) {
  return NextDirectorySortKey(OrderDirectoryWorkspaces(
      main_workspaces,
      GetIsolatedProfiles(g_browser_process ? g_browser_process->local_state()
                                            : nullptr)));
}

std::optional<PendingWorkspaceConversion> TakePendingWorkspaceConversion(
    const std::string& profile_dir) {
  auto& all = PendingConversions();
  auto it = all.find(profile_dir);
  if (it == all.end()) {
    return std::nullopt;
  }
  PendingWorkspaceConversion pending = std::move(it->second);
  all.erase(it);
  return pending;
}

void DeleteIsolatedWorkspaceProfile(Profile* profile,
                                    base::OnceCallback<void(bool)> done) {
  PrefService* local_state = g_browser_process->local_state();
  if (!profile || !local_state || !IsIsolatedWorkspaceProfile(profile)) {
    std::move(done).Run(false);
    return;
  }
  std::vector<content::WebContents*> pages;
  if (ProfileBrowserCollection* browsers =
          ProfileBrowserCollection::GetForProfile(profile)) {
    browsers->ForEach(
        [&pages](BrowserWindowInterface* browser) {
          TabStripModel* tabs = browser->GetTabStripModel();
          for (int i = 0; tabs && i < tabs->count(); ++i) {
            pages.push_back(tabs->GetWebContentsAt(i));
          }
          return true;
        },
        BrowserCollection::Order::kCreation);
  }
  const base::FilePath path = profile->GetPath();
  // The group object must outlive its answer; the callback owns it.
  auto holder = std::make_unique<std::unique_ptr<GroupPageClose>>();
  std::unique_ptr<GroupPageClose>* slot = holder.get();
  *slot = GroupPageClose::Ask(
      std::move(pages),
      base::BindOnce(
          [](std::unique_ptr<std::unique_ptr<GroupPageClose>> holder,
             base::FilePath path, base::OnceCallback<void(bool)> done,
             bool agreed) {
            PrefService* local_state = g_browser_process->local_state();
            ProfileManager* manager = g_browser_process->profile_manager();
            if (!agreed || !local_state || !manager) {
              // Destroy the group only after this callback returned.
              base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
                  FROM_HERE, std::move(holder));
              std::move(done).Run(false);
              return;
            }
            SetIsolatedProfileState(local_state, DirName(path),
                                    IsolatedProfileState::kDeleting);
            if (ProfileAttributesEntry* entry =
                    manager->GetProfileAttributesStorage()
                        .GetProfileAttributesWithPath(path)) {
              // Crash-safe: startup wipes ephemeral Profiles.
              entry->SetIsEphemeral(true);
            }
            local_state->CommitPendingWrite();
            // Pages already agreed; close them without a second question,
            // then let Chromium delete the Profile once its windows are gone.
            (**holder).ClosePages();
            // The main window this Workspace was presented over must come
            // back even when no hand-over watch survived a restart.
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(&ShowMainWindowsAfterIsolatedDeletion,
                                          DirName(path)));
            base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
                FROM_HERE, std::move(holder));
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE,
                base::BindOnce(
                    [](base::FilePath path) {
                      if (ProfileManager* manager =
                              g_browser_process->profile_manager()) {
                        manager->GetDeleteProfileHelper()
                            .MaybeScheduleProfileForDeletion(
                                path, base::DoNothing(),
                                ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
                      }
                    },
                    path),
                base::Seconds(1));
            std::move(done).Run(true);
          },
          std::move(holder), path, std::move(done)));
}

std::vector<IsolatedProfileEntry> GetOpenableIsolatedWorkspaces() {
  std::vector<IsolatedProfileEntry> result;
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  for (IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    if (entry.state != IsolatedProfileState::kDeleting &&
        entry.state != IsolatedProfileState::kConverting) {
      result.push_back(std::move(entry));
    }
  }
  return result;
}

bool IsIsolatedWorkspaceProfile(const Profile* profile) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  return profile && local_state &&
         FindIsolatedProfile(local_state, DirName(profile->GetPath()))
             .has_value();
}

void SweepIsolatedProfileRegistry() {
  static bool swept = false;
  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (swept || !profile_manager || !local_state) {
    return;
  }
  swept = true;
  std::set<std::string> existing;
  for (const ProfileAttributesEntry* attributes :
       profile_manager->GetProfileAttributesStorage().GetAllProfilesAttributes()) {
    existing.insert(DirName(attributes->GetPath()));
  }
  for (const std::string& dir :
       RemoveIsolatedProfilesNotIn(local_state, existing)) {
    LOG(WARNING) << "Ahoi removed the registry entry of missing profile "
                 << dir;
  }
  // An entry left `deleting` whose directory is already gone (the deletion
  // finished in an earlier run, after this sweep) is dropped, so a deleted
  // separated Workspace leaves no registry trace.
  for (const IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    if (entry.state != IsolatedProfileState::kDeleting) {
      continue;
    }
    const base::FilePath path =
        profile_manager->user_data_dir().AppendASCII(entry.profile_dir);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&base::DirectoryExists, path),
        base::BindOnce(
            [](std::string dir, bool exists) {
              PrefService* local_state = g_browser_process->local_state();
              if (!exists && local_state) {
                RemoveIsolatedProfile(local_state, dir);
              }
            },
            entry.profile_dir));
  }
  // Handoff 013 I2: an entry still `creating` after a restart comes from a
  // creation that crashed before its window initialized. If its tree was
  // already written the Workspace exists and becomes active; otherwise the
  // half-created Profile is deleted so no trace remains. Handoff 052: an
  // entry still `converting` is always deleted; its source Workspace was
  // never removed from the main Profile.
  for (const IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    if (entry.state != IsolatedProfileState::kCreating &&
        entry.state != IsolatedProfileState::kConverting) {
      continue;
    }
    const bool converting = entry.state == IsolatedProfileState::kConverting;
    const base::FilePath path =
        profile_manager->user_data_dir().AppendASCII(entry.profile_dir);
    if (profile_manager->GetProfileByPath(path)) {
      continue;  // Loaded now; its SessionBridge settles the state itself.
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&base::PathExists,
                       path.AppendASCII(kTabTreeDatabaseFilename)),
        base::BindOnce(
            [](base::FilePath path, std::string dir, bool converting,
               bool tree_written) {
              PrefService* local_state = g_browser_process->local_state();
              ProfileManager* manager = g_browser_process->profile_manager();
              if (!local_state || !manager) {
                return;
              }
              if (tree_written && !converting) {
                SetIsolatedProfileState(local_state, dir,
                                        IsolatedProfileState::kActive);
                return;
              }
              SetIsolatedProfileState(local_state, dir,
                                      IsolatedProfileState::kDeleting);
              if (ProfileAttributesEntry* attributes =
                      manager->GetProfileAttributesStorage()
                          .GetProfileAttributesWithPath(path)) {
                attributes->SetIsEphemeral(true);
              }
              local_state->CommitPendingWrite();
              manager->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
                  path, base::DoNothing(),
                  ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
            },
            path, entry.profile_dir, converting));
  }
}

}  // namespace ahoi::session
