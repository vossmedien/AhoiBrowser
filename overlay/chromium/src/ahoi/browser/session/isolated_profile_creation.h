// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_
#define AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/portable_workspace_structure.h"
#include "base/functional/callback.h"
#include "url/gurl.h"

#include "base/functional/callback_forward.h"

class Profile;

namespace ahoi::session {

// ADR 0011 level `isolated`, step 1. Creates a new Chromium Profile through
// ProfileManager (no Google sign-in, no profile picker), registers it in
// `ahoi.isolated_profiles` with a fresh Workspace identity before the Profile
// exists, and opens its window. The Profile's SessionBridge seeds its tree
// with that Workspace instead of the canonical Inbox. `done` receives false
// when the Profile could not be created; the registry entry is removed then.
// `sort_key` places the Workspace in the process-wide order
// (workspace_directory_order.h); callers pass NextDirectorySortKey().
void CreateIsolatedWorkspace(std::u16string name,
                             std::u16string icon,
                             std::optional<uint32_t> accent_argb,
                             std::string sort_key,
                             base::OnceCallback<void(bool)> done);

// ADR 0011 step 2 (handoff 052): moving a Workspace of the main Profile into
// a fully separated Workspace through the portable structure. Structure
// moves; logins, passwords and site data do not.
struct PendingWorkspaceConversion {
  PortableWorkspaceStructure structure;
  // Applied after the import; the imported Workspace record carries the
  // seeded default so it is identical to the new Profile's bootstrap.
  sync::SharedArchivePolicy archive_policy = sync::SharedArchivePolicy::kNever;
  // Open temporary pages of the source, reopened as new pages.
  std::vector<GURL> reopen_urls;
  // Runs once: true after the new Profile imported the structure and became
  // active; false when creation or import failed (that Profile is deleted).
  base::OnceCallback<void(bool)> done;
};

// Registers `presentation` (its `workspace_id`, name, icon, accent and
// `sort_key`; the directory is chosen here) in state `converting`, creates
// the Profile and opens its window. The new Profile's SessionBridge takes
// `pending` with TakePendingWorkspaceConversion() and imports it.
void ConvertToIsolatedWorkspace(const IsolatedProfileEntry& presentation,
                                PendingWorkspaceConversion pending);
std::optional<PendingWorkspaceConversion> TakePendingWorkspaceConversion(
    const std::string& profile_dir);

// Deletes a fully separated Workspace together with its Profile. Every page of
// the Profile is first asked as one before-unload group; a veto changes
// nothing and reports false. After agreement the registry entry is marked
// `deleting` and the Profile ephemeral (both committed), so a crash resumes
// through Chromium's startup cleanup, and Chromium's profile deletion removes
// windows, browsing data and the directory.
void DeleteIsolatedWorkspaceProfile(Profile* profile,
                                    base::OnceCallback<void(bool)> done);

// Fully separated Workspaces that can be opened (not being deleted and not
// still receiving a converted Workspace), in registry order. The shared
// switcher orders them with OrderDirectoryWorkspaces().
std::vector<IsolatedProfileEntry> GetOpenableIsolatedWorkspaces();

// True for a Profile that carries a fully separated Workspace.
bool IsIsolatedWorkspaceProfile(const Profile* profile);

// Once per browser process: drops registry entries whose Profile no longer
// exists (Chromium finished deleting it, or a creation crashed before the
// Profile was registered).
void SweepIsolatedProfileRegistry();

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_
