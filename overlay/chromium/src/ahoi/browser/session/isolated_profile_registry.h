// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_ISOLATED_PROFILE_REGISTRY_H_
#define AHOI_BROWSER_SESSION_ISOLATED_PROFILE_REGISTRY_H_

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/uuid.h"

class PrefRegistrySimple;
class PrefService;

namespace ahoi::session {

// ADR 0011 level `isolated`: every fully separated Workspace is its own
// Chromium Profile. This Local State list maps the Profile directory to the
// Workspace it carries, so the Workspace can be seeded before the Profile's
// first tree commit, found by the process-wide directory (step 2) and cleaned
// up after a crash. It holds presentation only, never website data.
inline constexpr char kIsolatedProfilesPref[] = "ahoi.isolated_profiles";
// Directory of the Profile whose window was presented by the last hand-over
// (ADR 0011 step 2); after a restart the other Profiles' windows in the same
// frame are hidden behind it again. Empty when no hand-over is active.
inline constexpr char kPresentedProfileDirPref[] =
    "ahoi.isolated_profiles_presented_dir";

enum class IsolatedProfileState {
  // Registered; the Profile's tree has not yet persisted the seeded Workspace.
  kCreating = 0,
  kActive = 1,
  // Deletion confirmed; Chromium's profile deletion is running or resumes.
  kDeleting = 2,
};

struct IsolatedProfileEntry {
  // Base name of the Profile directory inside the user data directory.
  std::string profile_dir;
  base::Uuid workspace_id;
  std::u16string name;
  std::u16string icon;
  std::optional<uint32_t> accent_argb;
  IsolatedProfileState state = IsolatedProfileState::kCreating;

  bool operator==(const IsolatedProfileEntry&) const = default;
};

void RegisterIsolatedProfileLocalState(PrefRegistrySimple* registry);

// Entries in stored order. Malformed entries are skipped, never repaired.
std::vector<IsolatedProfileEntry> GetIsolatedProfiles(
    const PrefService* local_state);
std::optional<IsolatedProfileEntry> FindIsolatedProfile(
    const PrefService* local_state,
    std::string_view profile_dir);

// Fails for an invalid entry or a directory or Workspace already listed.
bool AddIsolatedProfile(PrefService* local_state,
                        const IsolatedProfileEntry& entry);
bool SetIsolatedProfileState(PrefService* local_state,
                             std::string_view profile_dir,
                             IsolatedProfileState state);
bool RemoveIsolatedProfile(PrefService* local_state,
                           std::string_view profile_dir);

// Startup sweep: drops entries whose Profile no longer exists (deleted, or a
// creation that crashed before Chromium registered the Profile). Returns the
// removed directory names.
std::vector<std::string> RemoveIsolatedProfilesNotIn(
    PrefService* local_state,
    const std::set<std::string>& existing_profile_dirs);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_ISOLATED_PROFILE_REGISTRY_H_
