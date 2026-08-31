// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/zen/zen_application_discovery.h"

#include <CoreFoundation/CoreFoundation.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/apple/scoped_cftyperef.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_handle.h"
#include "base/process/process_iterator.h"

namespace ahoi::importer::zen {

namespace {

constexpr std::array<base::FilePath::StringViewType, 3> kZenBundleNames = {
    FILE_PATH_LITERAL("Zen.app"),
    FILE_PATH_LITERAL("Zen Browser.app"),
    FILE_PATH_LITERAL("Zen Twilight.app"),
};
constexpr int64_t kMaxInfoPlistBytes = 1024 * 1024;

bool IsKnownZenBundleName(const base::FilePath& path) {
  for (base::FilePath::StringViewType name : kZenBundleNames) {
    if (path.BaseName() == base::FilePath(name)) {
      return true;
    }
  }
  return false;
}

bool ReadSafeRegularFileWithMaxSize(const base::FilePath& path,
                                    int64_t max_size,
                                    std::string* output) {
  if (!output || max_size < 0) {
    return false;
  }
  base::ScopedFD fd(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat file_stat;
  if (!fd.is_valid() || HANDLE_EINTR(fstat(fd.get(), &file_stat)) != 0 ||
      !S_ISREG(file_stat.st_mode) || file_stat.st_size <= 0 ||
      file_stat.st_size > max_size) {
    return false;
  }

  output->clear();
  std::array<char, 4096> buffer{};
  for (;;) {
    const ssize_t bytes =
        HANDLE_EINTR(read(fd.get(), buffer.data(), buffer.size()));
    if (bytes < 0) {
      output->clear();
      return false;
    }
    if (bytes == 0) {
      return true;
    }
    if (bytes > max_size - static_cast<int64_t>(output->size())) {
      output->clear();
      return false;
    }
    output->append(buffer.data(), static_cast<size_t>(bytes));
  }
}

bool HasOfficialZenBundleIdentifier(const base::FilePath& info_plist) {
  std::string contents;
  if (!ReadSafeRegularFileWithMaxSize(info_plist, kMaxInfoPlistBytes,
                                      &contents)) {
    return false;
  }

  base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(contents.data()),
      static_cast<CFIndex>(contents.size())));
  if (!data) {
    return false;
  }
  CFErrorRef parse_error = nullptr;
  base::apple::ScopedCFTypeRef<CFPropertyListRef> property_list(
      CFPropertyListCreateWithData(kCFAllocatorDefault, data.get(),
                                   kCFPropertyListImmutable, nullptr,
                                   &parse_error));
  base::apple::ScopedCFTypeRef<CFErrorRef> scoped_parse_error(parse_error);
  if (!property_list || scoped_parse_error ||
      CFGetTypeID(property_list.get()) != CFDictionaryGetTypeID()) {
    return false;
  }

  const auto dictionary = static_cast<CFDictionaryRef>(property_list.get());
  const CFTypeRef identifier =
      CFDictionaryGetValue(dictionary, CFSTR("CFBundleIdentifier"));
  // Zen's official stable and Twilight macOS bundles currently share this
  // identifier. Do not add a Team ID until Zen publishes one that Ahoi can pin.
  return identifier && CFGetTypeID(identifier) == CFStringGetTypeID() &&
         CFStringCompare(static_cast<CFStringRef>(identifier),
                         CFSTR("app.zen-browser.zen"),
                         /*compareOptions=*/0) == kCFCompareEqualTo;
}

std::optional<base::FilePath> ZenBundleAncestor(
    const base::FilePath& executable) {
  if (executable.empty() || !executable.IsAbsolute() ||
      executable.ReferencesParent()) {
    return std::nullopt;
  }
  for (base::FilePath ancestor = executable.DirName(); !ancestor.empty();) {
    if (IsKnownZenBundleName(ancestor)) {
      return ancestor;
    }
    const base::FilePath parent = ancestor.DirName();
    if (parent == ancestor) {
      break;
    }
    ancestor = parent;
  }
  return std::nullopt;
}

std::vector<base::FilePath> DefaultApplicationRoots() {
  std::vector<base::FilePath> roots = {
      base::FilePath(FILE_PATH_LITERAL("/Applications"))};
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home) && !home.empty()) {
    roots.push_back(home.AppendASCII("Applications"));
  }
  return roots;
}

std::vector<base::FilePath> RunningExecutablePaths() {
  std::vector<base::FilePath> executables;
  base::ProcessIterator processes(/*filter=*/nullptr);
  while (const base::ProcessEntry* process = processes.NextProcessEntry()) {
    const base::FilePath executable =
        base::GetProcessExecutablePath(process->pid());
    if (!executable.empty()) {
      executables.push_back(executable);
    }
  }
  return executables;
}

}  // namespace

namespace internal {

bool IsZenBundleExecutablePath(const base::FilePath& executable) {
  const std::optional<base::FilePath> bundle = ZenBundleAncestor(executable);
  if (!bundle || !IsSafeZenApplicationBundle(*bundle)) {
    return false;
  }
  base::FilePath relative;
  if (!bundle->AppendRelativePath(executable, &relative)) {
    return false;
  }
  const std::vector<base::FilePath::StringType> components =
      relative.GetComponents();
  return components.size() >= 3u &&
         components[0] == FILE_PATH_LITERAL("Contents") &&
         (components[1] == FILE_PATH_LITERAL("MacOS") ||
          components[1] == FILE_PATH_LITERAL("Frameworks"));
}

bool IsSafeZenApplicationBundle(const base::FilePath& bundle_path) {
  const base::FilePath contents = bundle_path.AppendASCII("Contents");
  const base::FilePath macos = contents.AppendASCII("MacOS");
  if (!IsKnownZenBundleName(bundle_path) || base::IsLink(bundle_path) ||
      base::IsLink(contents) || base::IsLink(macos) ||
      !base::DirectoryExists(macos)) {
    return false;
  }
  const base::FilePath executable = macos.AppendASCII("zen");
  struct stat file_stat;
  return lstat(executable.value().c_str(), &file_stat) == 0 &&
         S_ISREG(file_stat.st_mode) &&
         HasOfficialZenBundleIdentifier(contents.AppendASCII("Info.plist"));
}

ZenApplicationState InspectZenApplicationAt(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables) {
  ZenApplicationState state;
  for (const base::FilePath& root : application_roots) {
    if (root.empty() || !root.IsAbsolute() || root.ReferencesParent() ||
        base::IsLink(root)) {
      continue;
    }
    for (base::FilePath::StringViewType name : kZenBundleNames) {
      const base::FilePath candidate = root.Append(name);
      if (IsSafeZenApplicationBundle(candidate)) {
        state.bundle_path = candidate;
        state.installed = true;
        break;
      }
    }
    if (state.installed) {
      break;
    }
  }
  for (const base::FilePath& executable : running_executables) {
    if (IsZenBundleExecutablePath(executable)) {
      state.running = true;
      break;
    }
  }
  return state;
}

}  // namespace internal

ZenImportAvailability GetZenImportAvailability(
    const ZenApplicationState& state) {
  if (!state.installed || state.bundle_path.empty()) {
    return ZenImportAvailability::kNotInstalled;
  }
  return state.running ? ZenImportAvailability::kSourceRunning
                       : ZenImportAvailability::kAvailable;
}

ZenApplicationState InspectDefaultZenApplication() {
  return internal::InspectZenApplicationAt(DefaultApplicationRoots(),
                                           RunningExecutablePaths());
}

}  // namespace ahoi::importer::zen
