// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/isolated_workspace_directory.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/isolated_profile_creation.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_directory_order.h"
#include "base/callback_list.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/base_window.h"
#include "ui/gfx/geometry/rect.h"

namespace ahoi::session {

namespace {

std::string DirName(const base::FilePath& path) {
  return path.BaseName().AsUTF8Unsafe();
}

std::set<std::string> IsolatedDirs() {
  std::set<std::string> dirs;
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  // Entries being deleted still name a separated Profile, never the main one.
  for (const IsolatedProfileEntry& entry : GetIsolatedProfiles(local_state)) {
    dirs.insert(entry.profile_dir);
  }
  return dirs;
}

BrowserWindowInterface* FindBrowserBySessionId(const SessionID& id) {
  BrowserWindowInterface* match = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&match, &id](BrowserWindowInterface* browser) {
        if (browser->GetSessionID() != id || browser->IsDeleteScheduled()) {
          return true;
        }
        match = browser;
        return false;
      });
  return match;
}

// Unlike ProfileBrowserCollection::FindTabbedBrowser this does not depend on
// the window being on the current workspace, so a window hidden by a
// hand-over is found and reused.
BrowserWindowInterface* FindMostRecentNormalBrowser(Profile* profile) {
  ProfileBrowserCollection* browsers =
      ProfileBrowserCollection::GetForProfile(profile);
  if (!browsers) {
    return nullptr;
  }
  BrowserWindowInterface* match = nullptr;
  browsers->ForEach(
      [&match](BrowserWindowInterface* browser) {
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            browser->IsDeleteScheduled() || !browser->GetWindow()) {
          return true;
        }
        match = browser;
        return false;
      },
      BrowserCollection::Order::kActivation);
  return match;
}

// Remembered across restarts (Local State), see RestoreHandOverAfterStartup.
void RecordPresentedProfile(Profile* profile) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || !local_state->FindPreference(kPresentedProfileDirPref)) {
    return;
  }
  local_state->SetString(kPresentedProfileDirPref,
                         profile ? DirName(profile->GetPath()) : std::string());
}

// Shows the hidden `source` again when the window presented in its place
// closes (for example the last window of a deleted separated Workspace), so
// the user is never left with only hidden windows. One per (presented,
// hidden) pair; it deletes itself when either window closes.
class HandOverWatch {
 public:
  static void Watch(BrowserWindowInterface* presented,
                    BrowserWindowInterface* hidden) {
    for (const std::unique_ptr<HandOverWatch>& watch : Watches()) {
      if (watch->presented_ == presented && watch->hidden_ == hidden) {
        return;
      }
    }
    Watches().push_back(
        base::WrapUnique(new HandOverWatch(presented, hidden)));
  }

  // The windows hidden behind windows of the Profile in `profile_dir`, in
  // watch order. Used when that Profile is deleted: exactly these windows
  // come back (handoff 042), not every main window.
  static std::vector<BrowserWindowInterface*> HiddenBehind(
      const std::string& profile_dir) {
    std::vector<BrowserWindowInterface*> hidden;
    for (const std::unique_ptr<HandOverWatch>& watch : Watches()) {
      if (watch->presented_ &&
          DirName(watch->presented_->GetProfile()->GetPath()) == profile_dir) {
        if (BrowserWindowInterface* window =
                FindBrowserBySessionId(watch->hidden_id_)) {
          hidden.push_back(window);
        }
      }
    }
    return hidden;
  }

 private:
  HandOverWatch(BrowserWindowInterface* presented,
                BrowserWindowInterface* hidden)
      : presented_(presented),
        hidden_(hidden),
        hidden_id_(hidden->GetSessionID()) {
    presented_closed_ = presented->RegisterBrowserDidClose(base::BindRepeating(
        &HandOverWatch::OnPresentedClosed, base::Unretained(this)));
    hidden_closed_ = hidden->RegisterBrowserDidClose(base::BindRepeating(
        &HandOverWatch::OnHiddenClosed, base::Unretained(this)));
  }

  static std::vector<std::unique_ptr<HandOverWatch>>& Watches() {
    static base::NoDestructor<std::vector<std::unique_ptr<HandOverWatch>>>
        watches;
    return *watches;
  }

  void OnPresentedClosed(BrowserWindowInterface*) {
    if (!browser_shutdown::HasShutdownStarted() &&
        !browser_shutdown::IsTryingToQuit()) {
      BrowserWindowInterface* hidden = FindBrowserBySessionId(hidden_id_);
      ui::BaseWindow* window = hidden ? hidden->GetWindow() : nullptr;
      if (window && !window->IsVisible() && !window->IsMinimized()) {
        window->Show();
        RecordPresentedProfile(hidden->GetProfile());
      }
    }
    Remove();
  }

  void OnHiddenClosed(BrowserWindowInterface*) { Remove(); }

  // Destroyed after the running close notification returned.
  void Remove() {
    presented_ = nullptr;
    hidden_ = nullptr;
    std::vector<std::unique_ptr<HandOverWatch>>& watches = Watches();
    auto it = std::find_if(watches.begin(), watches.end(),
                           [this](const std::unique_ptr<HandOverWatch>& w) {
                             return w.get() == this;
                           });
    if (it == watches.end()) {
      return;
    }
    std::unique_ptr<HandOverWatch> self = std::move(*it);
    watches.erase(it);
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(self));
  }

  raw_ptr<BrowserWindowInterface> presented_;
  raw_ptr<BrowserWindowInterface> hidden_;
  const SessionID hidden_id_;
  base::CallbackListSubscription presented_closed_;
  base::CallbackListSubscription hidden_closed_;
};


struct SourceFrame {
  SessionID id = SessionID::InvalidValue();
  gfx::Rect bounds;
  bool maximized = false;
};

SourceFrame CaptureFrame(BrowserWindowInterface* source) {
  SourceFrame frame;
  ui::BaseWindow* window = source ? source->GetWindow() : nullptr;
  if (!window) {
    return frame;
  }
  frame.id = source->GetSessionID();
  frame.maximized = window->IsMaximized();
  // A fullscreen source hands over its restored frame; the target does not
  // enter fullscreen.
  frame.bounds = frame.maximized || window->IsFullscreen()
                     ? window->GetRestoredBounds()
                     : window->GetBounds();
  return frame;
}

void FinishHandOver(SourceFrame frame,
                    base::OnceCallback<void(BrowserWindowInterface*)> done,
                    BrowserWindowInterface* target) {
  ui::BaseWindow* target_window = target ? target->GetWindow() : nullptr;
  if (!target_window) {
    std::move(done).Run(nullptr);
    return;
  }
  BrowserWindowInterface* source =
      frame.id.is_valid() ? FindBrowserBySessionId(frame.id) : nullptr;
  if (source == target) {
    source = nullptr;
  }
  if (target_window->IsMinimized()) {
    target_window->Restore();
  }
  if (source && !frame.bounds.IsEmpty()) {
    target_window->SetBounds(frame.bounds);
    if (frame.maximized) {
      target_window->Maximize();
    }
  }
  // Show() makes a hidden window visible again and activates it.
  target_window->Show();
  target_window->Activate();
  if (source && source->GetWindow()) {
    // A true hide, not Minimize(): nothing appears in the Dock, and the
    // window, its tabs and renderers stay alive, so switching back causes no
    // reload. Session restore still records the window as open.
    source->GetWindow()->Hide();
    HandOverWatch::Watch(target, source);
    RecordPresentedProfile(target->GetProfile());
  }
  std::move(done).Run(target);
}

}  // namespace

std::optional<std::string> ChooseMainProfileDir(
    const std::vector<MainProfileCandidate>& candidates,
    std::string_view last_used_dir,
    const std::set<std::string>& isolated_dirs) {
  const MainProfileCandidate* best = nullptr;
  auto rank = [last_used_dir](const MainProfileCandidate& candidate) {
    return std::make_pair(candidate.loaded,
                          candidate.profile_dir == last_used_dir);
  };
  for (const MainProfileCandidate& candidate : candidates) {
    if (candidate.profile_dir.empty() ||
        isolated_dirs.contains(candidate.profile_dir)) {
      continue;
    }
    if (!best || rank(candidate) > rank(*best) ||
        (rank(candidate) == rank(*best) &&
         candidate.active_time > best->active_time)) {
      best = &candidate;
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return best->profile_dir;
}

namespace {

// Loaded regular Profiles and the registered ones, each once.
std::vector<MainProfileCandidate> CollectCandidates(ProfileManager* manager) {
  std::vector<MainProfileCandidate> candidates;
  std::set<std::string> loaded;
  for (Profile* profile : manager->GetLoadedProfiles()) {
    if (profile->IsRegularProfile()) {
      loaded.insert(DirName(profile->GetPath()));
    }
  }
  for (const ProfileAttributesEntry* entry :
       manager->GetProfileAttributesStorage().GetAllProfilesAttributes()) {
    if (entry->IsEphemeral()) {
      continue;
    }
    const std::string dir = DirName(entry->GetPath());
    candidates.push_back({.profile_dir = dir,
                          .active_time = entry->GetActiveTime(),
                          .loaded = loaded.contains(dir)});
  }
  return candidates;
}

}  // namespace

Profile* GetLoadedMainProfile() {
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!manager) {
    return nullptr;
  }
  std::vector<MainProfileCandidate> candidates = CollectCandidates(manager);
  std::erase_if(candidates, [](const MainProfileCandidate& candidate) {
    return !candidate.loaded;
  });
  const std::optional<std::string> dir = ChooseMainProfileDir(
      candidates, DirName(manager->GetLastUsedProfileDir()), IsolatedDirs());
  if (!dir) {
    return nullptr;
  }
  Profile* profile =
      manager->GetProfileByPath(manager->user_data_dir().AppendASCII(*dir));
  DCHECK(!profile || !IsIsolatedWorkspaceProfile(profile));
  return profile;
}

void LoadMainProfile(base::OnceCallback<void(Profile*)> done) {
  if (Profile* profile = GetLoadedMainProfile()) {
    std::move(done).Run(profile);
    return;
  }
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!manager) {
    std::move(done).Run(nullptr);
    return;
  }
  const std::set<std::string> isolated = IsolatedDirs();
  std::optional<std::string> dir =
      ChooseMainProfileDir(CollectCandidates(manager),
                           DirName(manager->GetLastUsedProfileDir()), isolated);
  if (!dir) {
    const std::string initial = DirName(ProfileManager::GetInitialProfileDir());
    if (isolated.contains(initial)) {
      std::move(done).Run(nullptr);
      return;
    }
    dir = initial;
  }
  // Not profiles::LoadProfileAsync: it drops the callback on failure.
  manager->CreateProfileAsync(manager->user_data_dir().AppendASCII(*dir),
                              std::move(done));
}

namespace {

using HandOverCallback = base::OnceCallback<void(BrowserWindowInterface*)>;

// Handoff 146 #12: a hand-over repeated while the target Profile's window is
// still opening joins that opening instead of creating a second window. The
// requests finish in order, so the latest one decides the Workspace. An
// opening that never reports back stops blocking after kOpeningTimeout.
struct PendingOpening {
  base::TimeTicks started;
  std::vector<std::pair<SourceFrame, HandOverCallback>> requests;
};

constexpr base::TimeDelta kOpeningTimeout = base::Seconds(30);

std::map<base::FilePath, PendingOpening>& PendingOpenings() {
  static base::NoDestructor<std::map<base::FilePath, PendingOpening>> pending;
  return *pending;
}

void FinishOpening(base::FilePath path,
                   base::TimeTicks started,
                   BrowserWindowInterface* target) {
  auto it = PendingOpenings().find(path);
  if (it == PendingOpenings().end() || it->second.started != started) {
    return;
  }
  std::vector<std::pair<SourceFrame, HandOverCallback>> requests =
      std::move(it->second.requests);
  PendingOpenings().erase(it);
  for (auto& [frame, done] : requests) {
    FinishHandOver(std::move(frame), std::move(done), target);
  }
}

}  // namespace

void PresentProfileWindow(
    Profile* target,
    BrowserWindowInterface* source,
    base::OnceCallback<void(BrowserWindowInterface*)> done) {
  if (!target) {
    std::move(done).Run(nullptr);
    return;
  }
  SourceFrame frame = CaptureFrame(source);
  if (BrowserWindowInterface* existing = FindMostRecentNormalBrowser(target)) {
    FinishHandOver(std::move(frame), std::move(done), existing);
    return;
  }
  const base::FilePath path = target->GetPath();
  const base::TimeTicks now = base::TimeTicks::Now();
  auto [it, inserted] = PendingOpenings().try_emplace(path);
  if (!inserted && now - it->second.started < kOpeningTimeout) {
    it->second.requests.emplace_back(std::move(frame), std::move(done));
    return;
  }
  // A stale opening's requests are dropped with it; theirs never ran.
  it->second = PendingOpening{.started = now};
  it->second.requests.emplace_back(std::move(frame), std::move(done));
  // No window left: open one (always_create, the lookup above already ran),
  // with Chromium's normal startup and session restore for that Profile.
  profiles::OpenBrowserWindowForProfile(
      base::BindOnce(&FinishOpening, path, now),
      /*always_create=*/true, /*is_new_profile=*/false,
      /*open_command_line_urls=*/false, target);
}

namespace {

// Polls every 500 ms for up to 60 s; a presented window that never becomes
// visible leaves this Profile's windows as they are.
constexpr int kMaxHideAttempts = 120;

void TryHideBehindPresented(base::FilePath path,
                            std::string presented_dir,
                            int attempt) {
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!manager || browser_shutdown::HasShutdownStarted()) {
    return;
  }
  Profile* profile = manager->GetProfileByPath(path);
  Profile* presented_profile = manager->GetProfileByPath(
      manager->user_data_dir().AppendASCII(presented_dir));
  BrowserWindowInterface* presented =
      presented_profile ? FindMostRecentNormalBrowser(presented_profile)
                        : nullptr;
  ui::BaseWindow* presented_window =
      presented ? presented->GetWindow() : nullptr;
  if (!profile) {
    return;
  }
  if (!presented_window || !presented_window->IsVisible()) {
    if (attempt + 1 < kMaxHideAttempts) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&TryHideBehindPresented, std::move(path),
                         std::move(presented_dir), attempt + 1),
          base::Milliseconds(500));
    }
    return;
  }
  const gfx::Rect frame = presented_window->GetBounds();
  std::vector<BrowserWindowInterface*> to_hide;
  if (ProfileBrowserCollection* browsers =
          ProfileBrowserCollection::GetForProfile(profile)) {
    browsers->ForEach(
        [&to_hide, &frame](BrowserWindowInterface* browser) {
          ui::BaseWindow* window = browser->GetWindow();
          if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              window && window->IsVisible() &&
              window->GetBounds().Intersects(frame)) {
            to_hide.push_back(browser);
          }
          return true;
        },
        BrowserCollection::Order::kCreation);
  }
  for (BrowserWindowInterface* browser : to_hide) {
    browser->GetWindow()->Hide();
    HandOverWatch::Watch(presented, browser);
  }
}

}  // namespace

namespace {

bool HasVisibleNormalWindow(Profile* profile) {
  ProfileBrowserCollection* browsers =
      profile ? ProfileBrowserCollection::GetForProfile(profile) : nullptr;
  bool visible = false;
  if (browsers) {
    browsers->ForEach(
        [&visible](BrowserWindowInterface* browser) {
          ui::BaseWindow* window = browser->GetWindow();
          visible = browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
                    window && window->IsVisible();
          return !visible;
        },
        BrowserCollection::Order::kActivation);
  }
  return visible;
}

// Opens one main-Profile window, with Chromium's normal startup and session
// restore, when the main Profile has none on screen; loads it first if a
// restart presented only a separated Workspace.
void OpenMainWindowIfNoneVisible() {
  if (browser_shutdown::HasShutdownStarted() ||
      HasVisibleNormalWindow(GetLoadedMainProfile())) {
    return;
  }
  LoadMainProfile(base::BindOnce([](Profile* profile) {
    if (!profile || browser_shutdown::HasShutdownStarted() ||
        HasVisibleNormalWindow(profile)) {
      return;
    }
    profiles::OpenBrowserWindowForProfile(
        base::DoNothing(), /*always_create=*/false, /*is_new_profile=*/false,
        /*open_command_line_urls=*/false, profile);
  }));
}

}  // namespace

void ShowMainWindowsAfterIsolatedDeletion(
    const std::string& removed_profile_dir) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (local_state && local_state->FindPreference(kPresentedProfileDirPref) &&
      local_state->GetString(kPresentedProfileDirPref) == removed_profile_dir) {
    local_state->SetString(kPresentedProfileDirPref, std::string());
  }
  // Handoff 042: show exactly the windows the deleted Workspace was presented
  // over (in a chain main -> X -> Y, deleting Y shows X only). This runs right
  // after the pages were asked to close, while the watches still exist, and
  // does not rely on their close notification, which a restored hand-over
  // did not deliver on build 30. Without a watch, the main windows return.
  std::vector<BrowserWindowInterface*> behind =
      HandOverWatch::HiddenBehind(removed_profile_dir);
  if (!behind.empty()) {
    for (BrowserWindowInterface* browser : behind) {
      ui::BaseWindow* window = browser->GetWindow();
      if (window && !window->IsVisible() && !window->IsMinimized()) {
        window->Show();
        // The shown window is now the presented one, so a restart hides
        // the windows still behind it again (handoff 046).
        RecordPresentedProfile(browser->GetProfile());
      }
    }
    return;
  }
  Profile* main_profile = GetLoadedMainProfile();
  ProfileBrowserCollection* browsers =
      main_profile ? ProfileBrowserCollection::GetForProfile(main_profile)
                   : nullptr;
  if (!browsers) {
    // After a restart into the separated Workspace the main Profile may not
    // be loaded at all; deleting that Workspace must not leave the app
    // without a window (WS-ISO-16 on build 32).
    OpenMainWindowIfNoneVisible();
    return;
  }
  std::vector<BrowserWindowInterface*> hidden;
  browsers->ForEach(
      [&hidden](BrowserWindowInterface* browser) {
        ui::BaseWindow* window = browser->GetWindow();
        if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            window && !window->IsVisible() && !window->IsMinimized()) {
          hidden.push_back(browser);
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  // Most recently active last, so it ends up in front.
  for (auto it = hidden.rbegin(); it != hidden.rend(); ++it) {
    (*it)->GetWindow()->Show();
  }
  OpenMainWindowIfNoneVisible();
}

void RestoreHandOverAfterStartup(Profile* profile) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!profile || !local_state ||
      !local_state->FindPreference(kPresentedProfileDirPref)) {
    return;
  }
  const std::string presented_dir =
      local_state->GetString(kPresentedProfileDirPref);
  if (presented_dir.empty() || presented_dir == DirName(profile->GetPath())) {
    return;
  }
  // Restore of the other Profile may still be opening its window, which can
  // take long on a busy host (handoff 018): poll until it is visible.
  TryHideBehindPresented(profile->GetPath(), presented_dir, /*attempt=*/0);
}

void PresentIsolatedWorkspace(
    const std::string& profile_dir,
    BrowserWindowInterface* source,
    base::OnceCallback<void(BrowserWindowInterface*)> done) {
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  std::optional<IsolatedProfileEntry> entry =
      manager ? FindIsolatedProfile(g_browser_process->local_state(),
                                    profile_dir)
              : std::nullopt;
  if (!entry || entry->state == IsolatedProfileState::kDeleting) {
    std::move(done).Run(nullptr);
    return;
  }
  const SessionID source_id =
      source ? source->GetSessionID() : SessionID::InvalidValue();
  manager->CreateProfileAsync(
      manager->user_data_dir().AppendASCII(profile_dir),
      base::BindOnce(
          [](SessionID source_id,
             base::OnceCallback<void(BrowserWindowInterface*)> done,
             Profile* profile) {
            // The source may have closed while the Profile loaded.
            BrowserWindowInterface* source =
                source_id.is_valid() ? FindBrowserBySessionId(source_id)
                                     : nullptr;
            PresentProfileWindow(profile, source, std::move(done));
          },
          source_id, std::move(done)));
}

void LoadWorkspaceProfile(const std::string& profile_dir,
                          base::OnceCallback<void(Profile*)> done) {
  if (profile_dir.empty()) {
    LoadMainProfile(std::move(done));
    return;
  }
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  const std::optional<IsolatedProfileEntry> entry =
      manager ? FindIsolatedProfile(g_browser_process->local_state(),
                                    profile_dir)
              : std::nullopt;
  if (!entry || entry->state != IsolatedProfileState::kActive) {
    std::move(done).Run(nullptr);
    return;
  }
  manager->CreateProfileAsync(manager->user_data_dir().AppendASCII(profile_dir),
                              std::move(done));
}

Profile* FindLoadedProfile(const base::FilePath& path) {
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  Profile* profile = manager ? manager->GetProfileByPath(path) : nullptr;
  return profile && profile->IsRegularProfile() ? profile : nullptr;
}

BrowserWindowInterface* FindMostRecentNormalWindow(Profile* profile) {
  return profile ? FindMostRecentNormalBrowser(profile) : nullptr;
}

std::vector<OtherProfileWorkspace> ListOtherProfileWorkspaces(
    const Profile* own) {
  if (!own) {
    return {};
  }
  const std::string own_dir = DirName(own->GetPath());
  std::vector<DirectoryWorkspace> main_keys;
  std::map<base::Uuid, OtherProfileWorkspace> details;
  Profile* main_profile = GetLoadedMainProfile();
  SessionBridge* main_bridge =
      main_profile && main_profile != own
          ? SessionBridgeFactory::GetForProfile(main_profile)
          : nullptr;
  std::vector<tab_tree::Workspace> workspaces;
  if (main_bridge && main_bridge->is_ready() &&
      main_bridge->tab_tree_store()->GetWorkspaces(&workspaces) ==
          tab_tree::TabTreeStore::Result::kOk) {
    for (const tab_tree::Workspace& workspace : workspaces) {
      main_keys.push_back(
          {.workspace_id = workspace.id, .sort_key = workspace.sort_key});
      details.emplace(workspace.id,
                      OtherProfileWorkspace{.workspace_id = workspace.id,
                                            .name = workspace.name});
    }
  }
  std::vector<IsolatedProfileEntry> isolated = GetOpenableIsolatedWorkspaces();
  for (const IsolatedProfileEntry& entry : isolated) {
    if (entry.profile_dir != own_dir) {
      details.emplace(entry.workspace_id,
                      OtherProfileWorkspace{.workspace_id = entry.workspace_id,
                                            .name = entry.name,
                                            .profile_dir = entry.profile_dir});
    }
  }
  std::vector<OtherProfileWorkspace> result;
  for (const DirectoryWorkspace& key :
       OrderDirectoryWorkspaces(main_keys, isolated)) {
    if (auto it = details.find(key.workspace_id); it != details.end()) {
      result.push_back(it->second);
    }
  }
  return result;
}

}  // namespace ahoi::session
