// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_discovery.h"

#include <libproc.h>
#include <sys/proc_info.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/process_iterator.h"
#include "base/strings/string_util.h"

namespace ahoi::importer::arc {

namespace {

constexpr base::FilePath::CharType kArcDirectory[] = FILE_PATH_LITERAL("Arc");
constexpr base::FilePath::CharType kArcAppDirectory[] =
    FILE_PATH_LITERAL("Arc.app");
constexpr base::FilePath::CharType kContentsDirectory[] =
    FILE_PATH_LITERAL("Contents");
constexpr base::FilePath::CharType kMacOsDirectory[] =
    FILE_PATH_LITERAL("MacOS");
constexpr base::FilePath::CharType kSidebarFile[] =
    FILE_PATH_LITERAL("StorableSidebar.json");
constexpr base::FilePath::CharType kUserDataDirectory[] =
    FILE_PATH_LITERAL("User Data");

bool IsStructurallySafePath(const base::FilePath& path) {
  return !path.empty() && path.IsAbsolute() && !path.ReferencesParent();
}

bool IsArcMainExecutable(const base::FilePath& executable) {
  const base::FilePath mac_os = executable.DirName();
  const base::FilePath contents = mac_os.DirName();
  const base::FilePath app = contents.DirName();
  return executable.BaseName() == base::FilePath(kArcDirectory) &&
         mac_os.BaseName() == base::FilePath(kMacOsDirectory) &&
         contents.BaseName() == base::FilePath(kContentsDirectory) &&
         app.BaseName() == base::FilePath(kArcAppDirectory);
}

bool IsArcBundleProcess(const base::ProcessEntry& process) {
  if (process.cmd_line_args().empty()) {
    return false;
  }
  const base::FilePath executable =
      base::FilePath::FromUTF8Unsafe(process.cmd_line_args().front());
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

bool IsPathInside(const base::FilePath& parent, const base::FilePath& child) {
  return parent == child || parent.IsParent(child);
}

}  // namespace

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
  base::NamedProcessIterator iterator(base::FilePath(kArcDirectory).value(),
                                      /*filter=*/nullptr);
  while (const base::ProcessEntry* entry = iterator.NextProcessEntry()) {
    if (!entry->cmd_line_args().empty() &&
        IsArcMainExecutable(
            base::FilePath::FromUTF8Unsafe(entry->cmd_line_args().front()))) {
      return true;
    }
  }
  return false;
}

bool AreArcProfileFilesOpen(const ArcSource& source) {
  const base::FilePath user_data = source.arc_root.Append(kUserDataDirectory);
  if (source.arc_root.empty() || !source.arc_root.IsAbsolute() ||
      source.arc_root.ReferencesParent() || base::IsLink(source.arc_root) ||
      base::IsLink(user_data) || !base::DirectoryExists(user_data)) {
    return true;
  }

  base::ProcessIterator iterator(/*filter=*/nullptr);
  while (const base::ProcessEntry* process = iterator.NextProcessEntry()) {
    const bool arc_bundle_process = IsArcBundleProcess(*process);
    const int required_bytes =
        proc_pidinfo(process->pid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (required_bytes <= 0) {
      // Access to an Arc helper must be fail-closed: otherwise a detached
      // helper could still hold SQLite WAL/SHM handles while appearing safe.
      if (arc_bundle_process) {
        return true;
      }
      continue;
    }
    std::vector<proc_fdinfo> descriptors(static_cast<size_t>(required_bytes) /
                                         sizeof(proc_fdinfo));
    const int received_bytes = proc_pidinfo(process->pid(), PROC_PIDLISTFDS, 0,
                                            descriptors.data(), required_bytes);
    if (received_bytes <= 0) {
      if (arc_bundle_process) {
        return true;
      }
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
      if (!open_path.empty() && IsPathInside(user_data, open_path)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace ahoi::importer::arc
