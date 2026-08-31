// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TYPES_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ahoi/browser/tab_tree/tab_tree_model.h"
#include "base/files/file_path.h"
#include "base/time/time.h"

namespace ahoi::importer::arc {

// These limits are part of the import security boundary. Raising one requires
// a dedicated memory/CPU review and matching tests.
inline constexpr size_t kMaxSnapshotBytes = 16 * 1024 * 1024;
inline constexpr size_t kMaxWorkspaceCount = 128;
inline constexpr size_t kMaxBrowserProfileCount = 32;
inline constexpr size_t kMaxItemCount = 10'000;
inline constexpr size_t kMaxChildrenPerItem = 2'048;
inline constexpr size_t kMaxTreeDepth = 64;
inline constexpr size_t kMaxSourceIdentifierBytes = 512;
inline constexpr size_t kMaxTitleBytes = 4 * 1024;
inline constexpr size_t kMaxUrlBytes = 32 * 1024;

inline constexpr int kArcSourceSchemaVersion = 1;
inline constexpr int kArcSnapshotSchemaVersion = 1;
inline constexpr int kArcImportPlanSchemaVersion = 2;

enum class ArcImportStatus {
  kOk = 0,
  kNotFound,
  kInvalidPath,
  kUnsafeSymlink,
  kNotRegularFile,
  kIoError,
  kSourceChanged,
  kLimitExceeded,
  kInvalidJson,
  kUnsupportedSchema,
  kMissingRequiredField,
  kMalformedSerializedMap,
  kDuplicateIdentifier,
  kGraphViolation,
  kInvalidText,
  kNoImportableWorkspaces,
  kStalePreview,
  kConflict,
  kNoChanges,
  kTransactionFailed,
  kRuntimeFailed,
  kJournalError,
  kSourceInUse,
  kBackupError,
  // A durable prepared transaction was found. The importer remains locked
  // until its verified backup has been restored; native split uncertainty is
  // intentionally surfaced instead of being reported as recovered.
  kRecoveryRequired,
};

struct ArcBrowserProfile {
  std::string directory_name;
  base::FilePath path;

  bool operator==(const ArcBrowserProfile&) const = default;
};

struct ArcSource {
  base::FilePath arc_root;
  std::vector<ArcBrowserProfile> browser_profiles;
  base::FilePath sidebar_file;
  int64_t file_size = 0;
  base::Time last_modified;

  bool operator==(const ArcSource&) const = default;
};

struct ArcDiscoveryResult {
  ArcImportStatus status = ArcImportStatus::kNotFound;
  std::optional<ArcSource> source;
};

// An immutable in-memory copy separates parsing from a live Arc process that
// may rewrite StorableSidebar.json. It is never persisted by this component.
struct ArcImportSnapshot {
  int schema_version = kArcSnapshotSchemaVersion;
  base::FilePath source_path;
  int64_t source_size = 0;
  base::Time source_last_modified;
  std::array<uint8_t, 32> sha256{};
  std::string json;
};

struct ArcSnapshotResult {
  ArcImportStatus status = ArcImportStatus::kIoError;
  std::optional<ArcImportSnapshot> snapshot;
};

struct ArcImportStats {
  size_t source_workspace_count = 0;
  size_t source_item_count = 0;
  size_t imported_workspace_count = 0;
  size_t imported_folder_count = 0;
  size_t imported_page_count = 0;
  size_t imported_split_count = 0;
  size_t degraded_split_count = 0;
  size_t imported_global_top_app_count = 0;
  size_t skipped_unsafe_url_count = 0;
  size_t skipped_unsupported_item_count = 0;
  size_t ignored_unreachable_item_count = 0;

  bool operator==(const ArcImportStats&) const = default;
};

enum class ArcSplitOrientation {
  kHorizontal = 0,
  kVertical,
};

// Runtime reconstruction metadata for one Arc split. Every member refers to a
// saved-page node in `ArcImportPlan::tree`; no placeholder or phantom members
// are permitted. Ratios are finite, positive, normalized, and ordered exactly
// like `member_node_ids`.
struct ArcSplitDescriptor {
  base::Uuid folder_node_id;
  std::vector<base::Uuid> member_node_ids;
  ArcSplitOrientation orientation = ArcSplitOrientation::kHorizontal;
  base::Uuid focused_member_node_id;
  std::vector<double> normalized_ratios;

  bool operator==(const ArcSplitDescriptor&) const = default;
};

// This is a detached, deterministic import plan. Applying or merging it into
// a live profile is deliberately owned by a later confirmation/transaction
// seam, not by discovery or parsing.
struct ArcImportPlan {
  int schema_version = kArcImportPlanSchemaVersion;
  tab_tree::TabTreeSnapshot tree;
  std::vector<ArcSplitDescriptor> splits;
  ArcImportStats stats;

  bool operator==(const ArcImportPlan&) const = default;
};

struct ArcParseResult {
  ArcImportStatus status = ArcImportStatus::kInvalidJson;
  std::optional<ArcImportPlan> plan;
};

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TYPES_H_
