// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_ISOLATED_WORKSPACE_DIRECTORY_H_
#define AHOI_BROWSER_SESSION_ISOLATED_WORKSPACE_DIRECTORY_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"

class BrowserWindowInterface;
class Profile;

namespace ahoi::session {

// ADR 0011 step 2: the process-wide directory and window hand-over between
// the main Profile and the Profiles of fully separated Workspaces.

struct MainProfileCandidate {
  // Base name of the Profile directory inside the user data directory.
  std::string profile_dir;
  base::Time active_time;
  bool loaded = false;
};

// Picks the main Profile among `candidates`: never a fully separated one; a
// loaded Profile before an unloaded one, then the last used directory, then
// the most recently active, then input order. Chromium moves the last used
// directory to a separated Profile when its window is activated, so that
// alone does not identify the main Profile.
std::optional<std::string> ChooseMainProfileDir(
    const std::vector<MainProfileCandidate>& candidates,
    std::string_view last_used_dir,
    const std::set<std::string>& isolated_dirs);

// The loaded regular, non-separated Profile, or nullptr when none is loaded.
Profile* GetLoadedMainProfile();
// Runs `done` with the main Profile, loading it first if needed; nullptr when
// it cannot be loaded.
void LoadMainProfile(base::OnceCallback<void(Profile*)> done);

// Presents `target`'s most recently active normal window (a hidden one is
// shown again), or opens one, in `source`'s frame, then hides `source`. The
// hidden window keeps its tabs and web content; switching back shows it
// again, and it reappears if the presented window closes while it is still
// hidden. `done` receives the presented window or nullptr.
void PresentProfileWindow(
    Profile* target,
    BrowserWindowInterface* source,
    base::OnceCallback<void(BrowserWindowInterface*)> done);

// After a restart session restore shows every window again. Called once per
// Profile after its restore: when the last hand-over presented another
// Profile whose window is visible, this Profile's windows in that frame are
// hidden behind it again, with the same show-again guarantee.
void RestoreHandOverAfterStartup(Profile* profile);

// After the fully separated Workspace `removed_profile_dir` was deleted: the
// main Profile's windows hidden by a hand-over are shown again and the
// remembered hand-over is cleared, so deleting never leaves no window.
void ShowMainWindowsAfterIsolatedDeletion(const std::string& removed_profile_dir);

// Loads the Profile of the fully separated Workspace `profile_dir` and
// presents its window with PresentProfileWindow().
void PresentIsolatedWorkspace(
    const std::string& profile_dir,
    BrowserWindowInterface* source,
    base::OnceCallback<void(BrowserWindowInterface*)> done);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_ISOLATED_WORKSPACE_DIRECTORY_H_
