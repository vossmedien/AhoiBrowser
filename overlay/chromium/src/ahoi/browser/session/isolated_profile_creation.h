// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_
#define AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/functional/callback_forward.h"

class Profile;

namespace ahoi::session {

// ADR 0011 level `isolated`, step 1. Creates a new Chromium Profile through
// ProfileManager (no Google sign-in, no profile picker), registers it in
// `ahoi.isolated_profiles` with a fresh Workspace identity before the Profile
// exists, and opens its window. The Profile's SessionBridge seeds its tree
// with that Workspace instead of the canonical Inbox. `done` receives false
// when the Profile could not be created; the registry entry is removed then.
void CreateIsolatedWorkspace(std::u16string name,
                             std::u16string icon,
                             std::optional<uint32_t> accent_argb,
                             base::OnceCallback<void(bool)> done);

// Deletes a fully separated Workspace together with its Profile. Every page of
// the Profile is first asked as one before-unload group; a veto changes
// nothing and reports false. After agreement the registry entry is marked
// `deleting` and the Profile ephemeral (both committed), so a crash resumes
// through Chromium's startup cleanup, and Chromium's profile deletion removes
// windows, browsing data and the directory.
void DeleteIsolatedWorkspaceProfile(Profile* profile,
                                    base::OnceCallback<void(bool)> done);

// True for a Profile that carries a fully separated Workspace.
bool IsIsolatedWorkspaceProfile(const Profile* profile);

// Once per browser process: drops registry entries whose Profile no longer
// exists (Chromium finished deleting it, or a creation crashed before the
// Profile was registered).
void SweepIsolatedProfileRegistry();

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_ISOLATED_PROFILE_CREATION_H_
