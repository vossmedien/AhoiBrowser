// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_discovery.h"

#include <libproc.h>
#include <sys/proc_info.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/process/process_iterator.h"
#include "base/strings/string_util.h"

namespace ahoi::importer::arc {

namespace {

constexpr base::FilePath::CharType kArcDirectory[] = FILE_PATH_LITERAL("Arc");
constexpr base::FilePath::CharType kArcAppDirectory[] =
    FILE_PATH_LITERAL("Arc.app");
constexpr base::FilePath::CharType kSidebarFile[] =
    FILE_PATH_LITERAL("StorableSidebar.json");
constexpr base::FilePath::CharType kUserDataDirectory[] =
    FILE_PATH_LITERAL("User Data");
// These are the selected-profile inputs inspected or copied by discovery and
// backup. Keep the database set synchronized with CreateArcImportBackup().
constexpr std::array<std::string_view, 2> kArcProfileFiles = {
    "Preferences",
    "Bookmarks",
};
constexpr std::array<std::string_view, 3> kArcProfileDatabases = {
    "History",
    "Favicons",
    "Web Data",
};

bool IsStructurallySafePath(const base::FilePath& path) {
  return !path.empty() && path.IsAbsolute() && !path.ReferencesParent();
}

bool IsArcBundleProcess(const base::ProcessEntry& process) {
  if (internal::IsArcBundleExecutablePath(
          base::GetProcessExecutablePath(process.pid()))) {
    return true;
  }
  return !process.cmd_line_args().empty() &&
         internal::IsArcBundleExecutablePath(
             base::FilePath::FromUTF8Unsafe(
                 process.cmd_line_args().front()));
}

bool IsSelectableProfileName(const std::string& name) {
  if (name == "Default") {
    return true;
  }
  constexpr std::string_view kPrefix = "Profile ";
  if (!base::StartsWith(name, kPrefix) || name.size() == kPrefix.size()) {
    return false;
  }
  return std::ranges::all_of(name.substr(kPrefix.size()), [](char character) {
    return base::IsAsciiDigit(character);
  });
}

bool HasSafeOpenFileSource(const ArcSource& source) {
  const base::FilePath user_data = source.arc_root.Append(kUserDataDirectory);
  if (!IsStructurallySafePath(source.arc_root) ||
      source.sidebar_file != source.arc_root.Append(kSidebarFile) ||
      base::IsLink(source.arc_root) || base::IsLink(source.sidebar_file) ||
      base::IsLink(user_data) || !base::DirectoryExists(user_data) ||
      source.browser_profiles.empty()) {
    return false;
  }
  return std::ranges::all_of(
      source.browser_profiles, [&user_data](const ArcBrowserProfile& profile) {
        return IsStructurallySafePath(profile.path) &&
               IsSelectableProfileName(profile.directory_name) &&
               profile.path.DirName() == user_data &&
               profile.path.BaseName().MaybeAsASCII() ==
                   profile.directory_name &&
               !base::IsLink(profile.path) &&
               base::DirectoryExists(profile.path);
      });
}

}  // namespace

namespace internal {

bool IsArcBundleExecutablePath(const base::FilePath& executable) {
  if (!IsStructurallySafePath(executable)) {
    return false;
  }
  for (base::FilePath ancestor = executable.DirName(); !ancestor.empty();) {
    if (ancestor.BaseName() == base::FilePath(kArcAppDirectory)) {
      return true;
    }
    const base::FilePath parent = ancestor.DirName();
    if (parent == ancestor) {
      break;
    }
    ancestor = parent;
  }
  return false;
}

bool IsRelevantArcSourcePath(const ArcSource& source,
                             const base::FilePath& open_path) {
  if (open_path.empty()) {
    return false;
  }
  if (open_path == source.sidebar_file) {
    return true;
  }
  for (const ArcBrowserProfile& profile : source.browser_profiles) {
    for (std::string_view filename : kArcProfileFiles) {
      if (open_path == profile.path.AppendASCII(filename)) {
        return true;
      }
    }
    for (std::string_view filename : kArcProfileDatabases) {
      const base::FilePath database = profile.path.AppendASCII(filename);
      if (open_path == database ||
          open_path == base::FilePath(database.value() +
                                     FILE_PATH_LITERAL("-wal")) ||
          open_path == base::FilePath(database.value() +
                                     FILE_PATH_LITERAL("-shm"))) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace internal

ArcDiscoveryResult DiscoverArcSourceAt(
    const base::FilePath& application_support_dir) {
  if (!IsStructurallySafePath(application_support_dir)) {
    return {.status = ArcImportStatus::kInvalidPath};
  }
  if (base::IsLink(application_support_dir)) {
    return {.status = ArcImportStatus::kUnsafeSymlink};
  }
  if (!base::DirectoryExists(application_support_dir)) {
    return {.status = ArcImportStatus::kNotFound};
  }

  const base::FilePath arc_root = application_support_dir.Append(kArcDirectory);
  if (base::IsLink(arc_root)) {
    return {.status = ArcImportStatus::kUnsafeSymlink};
  }
  if (!base::DirectoryExists(arc_root)) {
    return {.status = ArcImportStatus::kNotFound};
  }

  const base::FilePath user_data = arc_root.Append(kUserDataDirectory);
  if (base::IsLink(user_data)) {
    return {.status = ArcImportStatus::kUnsafeSymlink};
  }
  if (!base::DirectoryExists(user_data)) {
    return {.status = ArcImportStatus::kNotFound};
  }
  std::vector<ArcBrowserProfile> browser_profiles;
  base::FileEnumerator profiles(user_data, /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = profiles.Next(); !path.empty();
       path = profiles.Next()) {
    const std::string name = path.BaseName().MaybeAsASCII();
    if (!IsSelectableProfileName(name)) {
      continue;
    }
    if (base::IsLink(path)) {
      return {.status = ArcImportStatus::kUnsafeSymlink};
    }
    const base::FilePath preferences = path.AppendASCII("Preferences");
    base::File::Info preferences_info;
    if (base::IsLink(preferences) ||
        !base::GetFileInfo(preferences, &preferences_info) ||
        preferences_info.is_directory || preferences_info.is_symbolic_link) {
      continue;
    }
    browser_profiles.push_back({.directory_name = name, .path = path});
    if (browser_profiles.size() > kMaxBrowserProfileCount) {
      return {.status = ArcImportStatus::kLimitExceeded};
    }
  }
  std::ranges::sort(browser_profiles, {}, &ArcBrowserProfile::directory_name);
  if (browser_profiles.empty()) {
    return {.status = ArcImportStatus::kNotFound};
  }

  const base::FilePath sidebar_file = arc_root.Append(kSidebarFile);
  if (base::IsLink(sidebar_file)) {
    return {.status = ArcImportStatus::kUnsafeSymlink};
  }

  base::File::Info info;
  if (!base::GetFileInfo(sidebar_file, &info)) {
    return {.status = ArcImportStatus::kNotFound};
  }
  if (info.is_directory || info.is_symbolic_link) {
    return {.status = info.is_symbolic_link ? ArcImportStatus::kUnsafeSymlink
                                            : ArcImportStatus::kNotRegularFile};
  }
  if (info.size < 0 || static_cast<uint64_t>(info.size) > kMaxSnapshotBytes) {
    return {.status = ArcImportStatus::kLimitExceeded};
  }

  return {
      .status = ArcImportStatus::kOk,
      .source =
          ArcSource{
              .arc_root = arc_root,
              .browser_profiles = std::move(browser_profiles),
              .sidebar_file = sidebar_file,
              .file_size = info.size,
              .last_modified = info.last_modified,
          },
  };
}

ArcDiscoveryResult DiscoverDefaultArcSource() {
  base::FilePath application_support_dir;
  if (!base::PathService::Get(base::DIR_APP_DATA, &application_support_dir)) {
    return {.status = ArcImportStatus::kIoError};
  }
  return DiscoverArcSourceAt(application_support_dir);
}

bool IsArcApplicationRunning() {
  base::ProcessIterator iterator(/*filter=*/nullptr);
  while (const base::ProcessEntry* entry = iterator.NextProcessEntry()) {
    if (IsArcBundleProcess(*entry)) {
      return true;
    }
  }
  return false;
}

bool AreArcProfileFilesOpen(const ArcSource& source) {
  if (!HasSafeOpenFileSource(source)) {
    return true;
  }

  base::ProcessIterator iterator(/*filter=*/nullptr);
  while (const base::ProcessEntry* process = iterator.NextProcessEntry()) {
    const bool arc_bundle_process = IsArcBundleProcess(*process);
    if (arc_bundle_process) {
      return true;
    }
    const int required_bytes =
        proc_pidinfo(process->pid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (required_bytes <= 0) {
      continue;
    }
    std::vector<proc_fdinfo> descriptors(static_cast<size_t>(required_bytes) /
                                         sizeof(proc_fdinfo));
    const int received_bytes = proc_pidinfo(process->pid(), PROC_PIDLISTFDS, 0,
                                            descriptors.data(), required_bytes);
    if (received_bytes <= 0) {
      continue;
    }
    descriptors.resize(static_cast<size_t>(received_bytes) /
                       sizeof(proc_fdinfo));
    for (const proc_fdinfo& descriptor : descriptors) {
      if (descriptor.proc_fdtype != PROX_FDTYPE_VNODE) {
        continue;
      }
      vnode_fdinfowithpath vnode_info{};
      const int vnode_bytes = proc_pidfdinfo(process->pid(), descriptor.proc_fd,
                                             PROC_PIDFDVNODEPATHINFO,
                                             &vnode_info, sizeof(vnode_info));
      if (vnode_bytes != static_cast<int>(sizeof(vnode_info))) {
        continue;
      }
      const base::FilePath open_path(vnode_info.pvip.vip_path);
      if (internal::IsRelevantArcSourcePath(source, open_path)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace ahoi::importer::arc
