// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SOURCE_MODEL_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SOURCE_MODEL_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/strings/string_number_conversions.h"
#include "url/gurl.h"

// The validated Arc sidebar source that ArcParser reads and ArcPlanBuilder
// turns into an import plan (split from arc_import_parser.cc, source line
// budget).
namespace ahoi::importer::arc::internal {

inline constexpr std::string_view kWorkspaceIdDomain = "arc-workspace-v1";
inline constexpr std::string_view kItemIdDomain = "arc-item-v1";
// The folders-as-workspaces layout uses its own identity domains. Its nodes
// sit at different parents and sort keys than the default layout, so sharing
// IDs would turn a layout change into an identity conflict.
inline constexpr std::string_view kFolderLayoutWorkspaceIdDomain =
    "arc-workspace-folders-v1";
inline constexpr std::string_view kFolderWorkspaceIdDomain =
    "arc-folder-workspace-v1";
inline constexpr std::string_view kFolderLayoutItemIdDomain =
    "arc-item-folders-v1";
inline constexpr size_t kMaxSplitMembers = 4;

enum class SpaceRootKind {
  kPinned,
  kUnpinned,
};

enum class SourceItemKind {
  kContainer,
  kFolder,
  kSplit,
  kTab,
  kUnsupported,
};

struct SourceSpace {
  std::string id;
  std::string title;
  // Always exactly {pinned root, unpinned root}, in this order.
  std::vector<std::string> root_container_ids;
  // The owning Arc browser profile's directory basename (WS-ISO-10).
  std::string arc_profile = kArcDefaultProfileName;
};

struct SourceItem {
  std::string id;
  std::optional<std::string> parent_id;
  std::vector<std::string> children;
  SourceItemKind kind = SourceItemKind::kUnsupported;
  std::string title;
  std::string saved_title;
  std::string url;
  std::optional<std::string> container_space_id;
  bool is_top_apps_container = false;
  bool split_metadata_valid = false;
  ArcSplitOrientation split_orientation = ArcSplitOrientation::kHorizontal;
  std::optional<std::string> split_focus_item_id;
  std::map<std::string, double> split_width_factors;
};

// Zero-padded ten-digit sibling order key.
inline std::string SortKey(size_t index) {
  std::string digits = base::NumberToString(index);
  if (digits.size() < 10) {
    digits.insert(0, 10 - digits.size(), '0');
  }
  return digits;
}

// Only credential-free HTTP(S) targets are imported.
inline bool IsSafeImportUrl(std::string_view value) {
  const GURL url(value);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password();
}

}  // namespace ahoi::importer::arc::internal

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SOURCE_MODEL_H_
