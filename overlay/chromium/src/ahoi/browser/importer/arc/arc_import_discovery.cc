// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_discovery.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_process_inspection.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_handle.h"
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
constexpr int64_t kMaxInfoPlistBytes = 1024 * 1024;
const CFStringRef kOfficialArcCodeRequirement = CFSTR(
    "anchor apple generic and "
    "identifier \"company.thebrowser.Browser\" and "
    "certificate leaf[subject.OU] = \"S6N382Y83G\"");
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

bool ReadSafeRegularFileWithMaxSize(const base::FilePath& path,
                                    int64_t maximum_bytes,
                                    std::string* output) {
  if (!output || maximum_bytes <= 0) {
    return false;
  }
  base::ScopedFD descriptor(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat state;
  if (!descriptor.is_valid() || fstat(descriptor.get(), &state) != 0 ||
      !S_ISREG(state.st_mode) || state.st_size <= 0 ||
      state.st_size > maximum_bytes) {
    return false;
  }
  output->clear();
  std::array<char, 4096> buffer{};
  for (;;) {
    const ssize_t bytes =
        HANDLE_EINTR(read(descriptor.get(), buffer.data(), buffer.size()));
    if (bytes < 0 ||
        (bytes > 0 &&
         bytes > maximum_bytes - static_cast<int64_t>(output->size()))) {
      output->clear();
      return false;
    }
    if (bytes == 0) {
      return true;
    }
    output->append(buffer.data(), static_cast<size_t>(bytes));
  }
}

bool HasOfficialArcBundleIdentity(const base::FilePath& info_plist) {
  std::string contents;
  if (!ReadSafeRegularFileWithMaxSize(info_plist, kMaxInfoPlistBytes,
                                      &contents)) {
    return false;
  }
  base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(contents.data()),
      static_cast<CFIndex>(contents.size())));
  if (!data) {
    return false;
  }
  CFErrorRef error = nullptr;
  base::apple::ScopedCFTypeRef<CFPropertyListRef> property_list(
      CFPropertyListCreateWithData(kCFAllocatorDefault, data.get(),
                                   kCFPropertyListImmutable, nullptr, &error));
  base::apple::ScopedCFTypeRef<CFErrorRef> scoped_error(error);
  if (!property_list || scoped_error ||
      CFGetTypeID(property_list.get()) != CFDictionaryGetTypeID()) {
    return false;
  }
  const auto dictionary = static_cast<CFDictionaryRef>(property_list.get());
  const CFTypeRef identifier =
      CFDictionaryGetValue(dictionary, CFSTR("CFBundleIdentifier"));
  const CFTypeRef executable =
      CFDictionaryGetValue(dictionary, CFSTR("CFBundleExecutable"));
  return identifier && executable &&
         CFGetTypeID(identifier) == CFStringGetTypeID() &&
         CFGetTypeID(executable) == CFStringGetTypeID() &&
         CFStringCompare(static_cast<CFStringRef>(identifier),
                         CFSTR("company.thebrowser.Browser"),
                         0) == kCFCompareEqualTo &&
         CFStringCompare(static_cast<CFStringRef>(executable), CFSTR("Arc"),
                         0) == kCFCompareEqualTo;
}

bool AuthenticatesOfficialArcBundle(const base::FilePath& bundle_path) {
  base::apple::ScopedCFTypeRef<CFURLRef> bundle_url =
      base::apple::FilePathToCFURL(bundle_path);
  if (!bundle_url) {
    return false;
  }
  base::apple::ScopedCFTypeRef<SecStaticCodeRef> static_code;
  if (SecStaticCodeCreateWithPath(bundle_url.get(), kSecCSDefaultFlags,
                                  static_code.InitializeInto()) !=
      errSecSuccess) {
    return false;
  }
  base::apple::ScopedCFTypeRef<SecRequirementRef> requirement;
  if (SecRequirementCreateWithString(
          kOfficialArcCodeRequirement, kSecCSDefaultFlags,
          requirement.InitializeInto()) != errSecSuccess) {
    return false;
  }
  return SecStaticCodeCheckValidity(
             static_code.get(),
             kSecCSCheckAllArchitectures | kSecCSCheckNestedCode,
             requirement.get()) == errSecSuccess;
}

std::optional<base::FilePath> ArcBundleAncestor(
    const base::FilePath& executable) {
  if (executable.empty() || !executable.IsAbsolute() ||
      executable.ReferencesParent()) {
    return std::nullopt;
  }
  for (base::FilePath ancestor = executable.DirName(); !ancestor.empty();) {
    if (ancestor.BaseName() == base::FilePath(kArcAppDirectory)) {
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

bool ExecutableBelongsToBundle(const base::FilePath& executable,
                               const base::FilePath& bundle) {
  const std::optional<base::FilePath> ancestor = ArcBundleAncestor(executable);
  if (!ancestor || *ancestor != bundle) {
    return false;
  }
  base::FilePath relative;
  if (!bundle.AppendRelativePath(executable, &relative)) {
    return false;
  }
  const std::vector<base::FilePath::StringType> components =
      relative.GetComponents();
  return components.size() >= 3u &&
         components[0] == FILE_PATH_LITERAL("Contents") &&
         (components[1] == FILE_PATH_LITERAL("MacOS") ||
          components[1] == FILE_PATH_LITERAL("Frameworks"));
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

bool IsStructurallySafePath(const base::FilePath& path) {
  return !path.empty() && path.IsAbsolute() && !path.ReferencesParent();
}

using process_inspection::InspectOpenFilesWithRetry;
using process_inspection::ListAllPids;
using process_inspection::ObserveProcess;
using process_inspection::OpenFileInspectionResult;
using process_inspection::ProcessObservation;

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
  const std::optional<base::FilePath> bundle = ArcBundleAncestor(executable);
  if (!bundle) {
    return false;
  }
  return ExecutableBelongsToBundle(executable, *bundle);
}

bool IsSafeArcApplicationBundleWithAuthenticator(
    const base::FilePath& bundle_path,
    const ArcBundleAuthenticationCallback& bundle_authenticator) {
  const base::FilePath contents = bundle_path.AppendASCII("Contents");
  const base::FilePath macos = contents.AppendASCII("MacOS");
  const base::FilePath executable = macos.AppendASCII("Arc");
  struct stat executable_state;
  return bundle_path.BaseName() == base::FilePath(kArcAppDirectory) &&
         !base::IsLink(bundle_path) && !base::IsLink(contents) &&
         !base::IsLink(macos) && !base::IsLink(executable) &&
         base::DirectoryExists(macos) &&
         lstat(executable.value().c_str(), &executable_state) == 0 &&
         S_ISREG(executable_state.st_mode) &&
         (executable_state.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 &&
         HasOfficialArcBundleIdentity(contents.AppendASCII("Info.plist")) &&
         bundle_authenticator && bundle_authenticator.Run(bundle_path);
}

bool IsSafeArcApplicationBundle(const base::FilePath& bundle_path) {
  return IsSafeArcApplicationBundleWithAuthenticator(
      bundle_path, base::BindRepeating(&AuthenticatesOfficialArcBundle));
}

ArcApplicationState InspectArcApplicationAtWithAuthenticator(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables,
    const ArcBundleAuthenticationCallback& bundle_authenticator) {
  ArcApplicationState state;
  std::vector<base::FilePath> candidates;
  for (const base::FilePath& root : application_roots) {
    if (!IsStructurallySafePath(root) || base::IsLink(root)) {
      continue;
    }
    const base::FilePath candidate = root.Append(kArcAppDirectory);
    if (IsSafeArcApplicationBundleWithAuthenticator(candidate,
                                                    bundle_authenticator)) {
      candidates.push_back(candidate);
    }
  }
  if (candidates.empty()) {
    return state;
  }
  state.installed = true;
  state.bundle_path = candidates.front();
  for (const base::FilePath& candidate : candidates) {
    if (std::ranges::any_of(
            running_executables, [&](const base::FilePath& executable) {
              return ExecutableBelongsToBundle(executable, candidate);
            })) {
      // Prefer the authenticated bundle that actually owns a running
      // executable. This avoids missing a user-local Arc when a second,
      // system-wide installation is encountered first.
      state.bundle_path = candidate;
      state.running = true;
      break;
    }
  }
  return state;
}

ArcApplicationState InspectArcApplicationAt(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables) {
  return InspectArcApplicationAtWithAuthenticator(
      application_roots, running_executables,
      base::BindRepeating(&AuthenticatesOfficialArcBundle));
}

ArcApplicationState InspectArcApplicationAtForTesting(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables,
    ArcBundleAuthenticationCallback bundle_authenticator) {
  return InspectArcApplicationAtWithAuthenticator(
      application_roots, running_executables, bundle_authenticator);
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
          open_path ==
              base::FilePath(database.value() + FILE_PATH_LITERAL("-wal")) ||
          open_path ==
              base::FilePath(database.value() + FILE_PATH_LITERAL("-shm"))) {
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

base::FilePath GetArcE2ESourceDirectory() {
#if !defined(OFFICIAL_BUILD)
  if (base::CommandLine::InitializedForCurrentProcess()) {
    const auto* command = base::CommandLine::ForCurrentProcess();
    const auto source = command->GetSwitchValuePath("ahoi-e2e-arc-source-directory");
    const auto target = command->GetSwitchValuePath("user-data-dir");
    const auto root = source.DirName();
    const auto protected_directory = [](const base::FilePath& path) {
      struct stat st;
      return lstat(path.value().c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
             st.st_uid == getuid() && (st.st_mode & 0077) == 0;
    };
    if (!source.empty() && root.DirName() == base::FilePath("/private/tmp") &&
        root.BaseName().MaybeAsASCII().starts_with("ahoi-arc-history-e2e-") &&
        target.DirName() == root && protected_directory(root) &&
        protected_directory(source) && protected_directory(target)) {
      return source;
    }
  }
#endif
  return {};
}

ArcDiscoveryResult DiscoverDefaultArcSource() {
  if (!InspectDefaultArcApplication().installed) {
    return {.status = ArcImportStatus::kNotFound};
  }
  base::FilePath application_support_dir;
  if (!base::PathService::Get(base::DIR_APP_DATA, &application_support_dir)) {
    return {.status = ArcImportStatus::kIoError};
  }
  return DiscoverArcSourceAt(application_support_dir);
}

bool IsArcApplicationRunning() {
  return InspectDefaultArcApplication().running;
}

ArcApplicationState InspectDefaultArcApplication() {
  const std::optional<std::vector<pid_t>> pids = ListAllPids();
  std::vector<base::FilePath> executables;
  if (pids) {
    executables.reserve(pids->size());
    for (pid_t pid : *pids) {
      const base::FilePath executable = base::GetProcessExecutablePath(pid);
      if (!executable.empty()) {
        executables.push_back(executable);
      }
    }
  }
  ArcApplicationState state =
      internal::InspectArcApplicationAt(DefaultApplicationRoots(), executables);
  if (state.installed && !pids) {
    // Process enumeration is part of the source-mutability gate. Once a real
    // Arc bundle is installed, inability to inspect processes fails closed.
    state.running = true;
  }
  return state;
}

ArcImportAvailability GetDefaultArcImportAvailability() {
  const auto fixture = GetArcE2ESourceDirectory();
  if (!fixture.empty()) {
    return DiscoverArcSourceAt(fixture).status == ArcImportStatus::kOk
               ? ArcImportAvailability::kAvailable
               : ArcImportAvailability::kNoSafeProfiles;
  }
  const ArcApplicationState application = InspectDefaultArcApplication();
  if (!application.installed) {
    return ArcImportAvailability::kNotInstalled;
  }
  if (application.running) {
    return ArcImportAvailability::kSourceRunning;
  }
  base::FilePath application_support_dir;
  if (!base::PathService::Get(base::DIR_APP_DATA, &application_support_dir) ||
      DiscoverArcSourceAt(application_support_dir).status !=
          ArcImportStatus::kOk) {
    return ArcImportAvailability::kNoSafeProfiles;
  }
  return ArcImportAvailability::kAvailable;
}

bool AreArcProfileFilesOpen(const ArcSource& source) {
  if (!HasSafeOpenFileSource(source)) {
    return true;
  }

  const std::optional<std::vector<pid_t>> pids = ListAllPids();
  if (!pids) {
    return true;
  }

  std::vector<base::FilePath> running_executables;
  running_executables.reserve(pids->size());
  for (pid_t pid : *pids) {
    const base::FilePath executable = base::GetProcessExecutablePath(pid);
    if (!executable.empty()) {
      running_executables.push_back(executable);
    }
  }
  if (internal::InspectArcApplicationAt(DefaultApplicationRoots(),
                                        running_executables)
          .running) {
    return true;
  }

  for (pid_t pid : *pids) {
    const ProcessObservation observation = ObserveProcess(pid);
    if (!observation.metadata) {
      // Arc processes already block above through their bundle executable.
      // Treat an unrelated process that cannot be inspected as inconclusive,
      // not as positive evidence that Arc still owns a source file. This is
      // common for sandboxed macOS helpers and otherwise makes a real import
      // impossible while those helpers are alive.
      continue;
    }
    const OpenFileInspectionResult result =
        InspectOpenFilesWithRetry(pid, *observation.metadata, source);
    const internal::OpenFileInspectionEvidence evidence =
        result == OpenFileInspectionResult::kRelevantFileOpen
            ? internal::OpenFileInspectionEvidence::kRelevantSourceHandle
        : result == OpenFileInspectionResult::kFailed
            ? internal::OpenFileInspectionEvidence::kInspectionInconclusive
            : internal::OpenFileInspectionEvidence::kNoRelevantSourceHandle;
    if (internal::ShouldBlockOnOpenFileInspectionEvidence(
            evidence, observation.metadata->ownership)) {
      return true;
    }
    // An incomplete descriptor snapshot remains inconclusive. The importer
    // still blocks every positively identified Arc executable and every
    // readable relevant handle. CaptureArcSnapshot() verifies the sidebar
    // identity and metadata before and after reading; backup creation hashes
    // and verifies every copied source and revalidates the sidebar token.
    // Those consistency gates safely catch mutation without turning unrelated
    // high-churn browser/helper processes into permanent false positives.
  }
  return false;
}

}  // namespace ahoi::importer::arc
