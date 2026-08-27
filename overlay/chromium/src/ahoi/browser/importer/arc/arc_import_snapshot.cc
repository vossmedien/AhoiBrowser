// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_snapshot.h"

#include <cstdint>
#include <string>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

constexpr base::FilePath::CharType kSidebarFile[] =
    FILE_PATH_LITERAL("StorableSidebar.json");
constexpr base::FilePath::CharType kArcDirectory[] = FILE_PATH_LITERAL("Arc");
constexpr base::FilePath::CharType kUserDataDirectory[] =
    FILE_PATH_LITERAL("User Data");

ArcImportStatus ValidateSourcePath(const ArcSource& source) {
  if (source.arc_root.empty() || source.browser_profiles.empty() ||
      source.sidebar_file.empty() || !source.arc_root.IsAbsolute() ||
      !source.sidebar_file.IsAbsolute() || source.arc_root.ReferencesParent() ||
      source.sidebar_file.ReferencesParent() ||
      source.arc_root.BaseName() != base::FilePath(kArcDirectory) ||
      source.sidebar_file != source.arc_root.Append(kSidebarFile)) {
    return ArcImportStatus::kInvalidPath;
  }
  if (base::IsLink(source.arc_root.DirName()) ||
      base::IsLink(source.arc_root) ||
      base::IsLink(source.arc_root.Append(kUserDataDirectory)) ||
      base::IsLink(source.sidebar_file)) {
    return ArcImportStatus::kUnsafeSymlink;
  }
  if (!base::DirectoryExists(source.arc_root)) {
    return ArcImportStatus::kNotFound;
  }
  const base::FilePath user_data = source.arc_root.Append(kUserDataDirectory);
  for (const ArcBrowserProfile& profile : source.browser_profiles) {
    if (profile.directory_name.empty() || !profile.path.IsAbsolute() ||
        profile.path.ReferencesParent() ||
        profile.path != user_data.AppendASCII(profile.directory_name) ||
        base::IsLink(profile.path) || !base::DirectoryExists(profile.path)) {
      return ArcImportStatus::kInvalidPath;
    }
  }
  return ArcImportStatus::kOk;
}

bool IsRegularBoundedFile(const base::File::Info& info) {
  return !info.is_directory && !info.is_symbolic_link && info.size >= 0 &&
         static_cast<uint64_t>(info.size) <= kMaxSnapshotBytes;
}

}  // namespace

ArcSnapshotResult CaptureArcSnapshot(const ArcSource& source) {
  const ArcImportStatus path_status = ValidateSourcePath(source);
  if (path_status != ArcImportStatus::kOk) {
    return {.status = path_status};
  }

  base::File::Info path_info_before;
  if (!base::GetFileInfo(source.sidebar_file, &path_info_before)) {
    return {.status = ArcImportStatus::kNotFound};
  }
  if (!IsRegularBoundedFile(path_info_before)) {
    return {.status = path_info_before.is_directory
                          ? ArcImportStatus::kNotRegularFile
                          : ArcImportStatus::kLimitExceeded};
  }
  if (path_info_before.size != source.file_size ||
      path_info_before.last_modified != source.last_modified) {
    return {.status = ArcImportStatus::kSourceChanged};
  }

  base::File file(source.sidebar_file, base::File::FLAG_OPEN |
                                           base::File::FLAG_READ |
                                           base::File::FLAG_NO_FOLLOW);
  if (!file.IsValid()) {
    return {.status = ArcImportStatus::kIoError};
  }

  base::File::Info handle_info_before;
  if (!file.GetInfo(&handle_info_before) ||
      !IsRegularBoundedFile(handle_info_before)) {
    return {.status = ArcImportStatus::kNotRegularFile};
  }

  std::string json(static_cast<size_t>(handle_info_before.size), '\0');
  const std::optional<size_t> bytes_read =
      file.Read(0, base::as_writable_byte_span(json));
  if (!bytes_read.has_value() || *bytes_read != json.size()) {
    return {.status = ArcImportStatus::kIoError};
  }

  base::File::Info handle_info_after;
  base::File::Info path_info_after;
  if (!file.GetInfo(&handle_info_after) ||
      !base::GetFileInfo(source.sidebar_file, &path_info_after)) {
    return {.status = ArcImportStatus::kSourceChanged};
  }
  if (ValidateSourcePath(source) != ArcImportStatus::kOk ||
      handle_info_before.size != handle_info_after.size ||
      handle_info_before.last_modified != handle_info_after.last_modified ||
      handle_info_before.creation_time != handle_info_after.creation_time ||
      path_info_before.size != path_info_after.size ||
      path_info_before.last_modified != path_info_after.last_modified ||
      path_info_before.creation_time != path_info_after.creation_time ||
      handle_info_after.size != path_info_after.size ||
      handle_info_after.last_modified != path_info_after.last_modified ||
      handle_info_after.creation_time != path_info_after.creation_time) {
    return {.status = ArcImportStatus::kSourceChanged};
  }

  return {
      .status = ArcImportStatus::kOk,
      .snapshot =
          ArcImportSnapshot{
              .source_path = source.sidebar_file,
              .source_size = handle_info_after.size,
              .source_last_modified = handle_info_after.last_modified,
              .sha256 = crypto::hash::Sha256(json),
              .json = std::move(json),
          },
  };
}

}  // namespace ahoi::importer::arc
