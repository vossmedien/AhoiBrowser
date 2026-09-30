// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROFILE_MAPPING_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROFILE_MAPPING_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "ahoi/browser/tab_tree/portable_workspace_selection.h"
#include "base/uuid.h"

// ADR 0011 WS-ISO-10: the user can map an Arc browser profile to one fully
// separated Workspace (its own Chromium Profile) instead of the running Ahoi
// Profile. This unit is pure: it only decides which planned Workspaces stay
// in the main plan and which become the tree of a separated Workspace. It
// never touches a Profile, the registry or a store.
namespace ahoi::importer::arc {

// Identity domains of the separated layout. The separated Workspace's ID is
// derived from the Arc profile's directory name alone, so a repeated import
// of the same Arc profile finds the Workspace it created before.
inline constexpr std::string_view kArcSeparatedWorkspaceIdDomain =
    "arc-profile-workspace-v1";
inline constexpr std::string_view kArcSeparatedSpaceFolderIdDomain =
    "arc-profile-space-folder-v1";

struct ArcSeparatedWorkspacePlan {
  std::string arc_profile;
  base::Uuid workspace_id;
  std::u16string name;
  // Exactly one Workspace (`workspace_id`, sort key "0", no icon or accent,
  // default archive policy: identical to the seed the new Profile's
  // SessionBridge writes from its registry entry) and its nodes.
  tab_tree::TabTreeSnapshot tree;
  size_t space_count = 0;
  size_t folder_count = 0;
  size_t page_count = 0;

  bool operator==(const ArcSeparatedWorkspacePlan&) const = default;
};

struct ArcProfileMapping {
  // The plan for the running Profile without the separated profiles'
  // Workspaces, nodes and splits. Its stats are recounted by the merge.
  ArcImportPlan main_plan;
  // One entry per separated Arc profile, in `plan.arc_profiles` order.
  std::vector<ArcSeparatedWorkspacePlan> separated;
};

// Invalid for an empty or unusable name.
base::Uuid ArcSeparatedWorkspaceId(std::string_view arc_profile);

// "Arc – <profile>", or the only space's title for a one-space profile.
std::u16string ArcSeparatedWorkspaceName(
    const ArcImportProfileSpaces& profile);

// True when every entry is unique and names an Arc profile of `plan` that
// owns spaces. An empty list is valid (nothing is separated).
bool IsValidArcSeparatedProfiles(const ArcImportPlan& plan,
                                 const std::vector<std::string>& separated);

// Requires IsValidArcSeparatedProfiles(). Each separated profile becomes one
// Workspace: a profile with one planned Workspace keeps that Workspace's
// top-level structure at the root; with several (several spaces, or folder
// Workspaces) each becomes a top-level folder named after it, in plan order.
// Node IDs are the plan's deterministic IDs. Splits are not reconstructed
// there: their members stay inside a plain folder.
ArcProfileMapping MapArcImportPlanByProfile(
    const ArcImportPlan& plan,
    const std::vector<std::string>& separated);

// The portable selection that the new Profile imports
// (CommitPortableWorkspaceImport). nullopt when the tree is unusable.
std::optional<tab_tree::PortableWorkspaceSelection>
SelectArcSeparatedPortableTree(const ArcSeparatedWorkspacePlan& plan);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROFILE_MAPPING_H_
