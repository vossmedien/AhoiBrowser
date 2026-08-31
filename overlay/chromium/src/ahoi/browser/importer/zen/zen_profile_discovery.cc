// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/zen/zen_profile_discovery.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/ini_parser.h"

namespace ahoi::importer::zen {

namespace {

constexpr size_t kMaxProfilesIniBytes = 1024 * 1024;
constexpr int64_t kMaxCompatibilityIniBytes = 1024 * 1024;
constexpr int kMaxProfiles = 32;
constexpr int64_t kMaxSessionStoreBytes = 64 * 1024 * 1024;
constexpr std::array<uint8_t, 8> kMozLz4Header = {'m', 'o', 'z', 'L',
                                                  'z', '4', '0', 0};

bool IsSafeRegularFile(const base::FilePath& path) {
  struct stat file_stat;
  return lstat(path.value().c_str(), &file_stat) == 0 &&
         S_ISREG(file_stat.st_mode);
}

base::ScopedFD OpenSafeRegularFile(const base::FilePath& path,
                                   struct stat* file_stat) {
  base::ScopedFD fd(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  if (!fd.is_valid() || fstat(fd.get(), file_stat) != 0 ||
      !S_ISREG(file_stat->st_mode)) {
    return base::ScopedFD();
  }
  return fd;
}

bool ReadSafeRegularFileWithMaxSize(const base::FilePath& path,
                                    int64_t max_size,
                                    std::string* output) {
  if (!output || max_size < 0) {
    return false;
  }
  struct stat file_stat;
  base::ScopedFD fd = OpenSafeRegularFile(path, &file_stat);
  if (!fd.is_valid() || file_stat.st_size < 0 || file_stat.st_size > max_size) {
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

bool HasSymlinkComponent(const base::FilePath& root,
                         const base::FilePath& child) {
  base::FilePath relative;
  if (!root.AppendRelativePath(child, &relative)) {
    return true;
  }
  base::FilePath current = root;
  for (const auto& component : relative.GetComponents()) {
    current = current.Append(component);
    if (base::IsLink(current)) {
      return true;
    }
  }
  return false;
}

std::optional<base::FilePath> ResolveProfilePath(
    const base::FilePath& root,
    const base::DictValue& dictionary,
    const std::string& section) {
  const std::string* path_value =
      dictionary.FindStringByDottedPath(section + ".Path");
  const std::string* relative_value =
      dictionary.FindStringByDottedPath(section + ".IsRelative");
  if (!path_value || !relative_value || path_value->empty()) {
    return std::nullopt;
  }

  base::FilePath configured = base::FilePath::FromUTF8Unsafe(*path_value);
  if (configured.empty() || configured.ReferencesParent()) {
    return std::nullopt;
  }
  if (*relative_value == "1") {
    if (configured.IsAbsolute()) {
      return std::nullopt;
    }
    configured = root.Append(configured);
  } else if (*relative_value != "0" || !configured.IsAbsolute()) {
    return std::nullopt;
  }

  const base::FilePath absolute_root = base::MakeAbsoluteFilePath(root);
  const base::FilePath absolute_profile =
      base::MakeAbsoluteFilePath(configured);
  if (absolute_root.empty() || absolute_profile.empty() ||
      !absolute_root.IsParent(absolute_profile) ||
      HasSymlinkComponent(absolute_root, configured) ||
      !base::DirectoryExists(absolute_profile)) {
    return std::nullopt;
  }
  return absolute_profile;
}

ZenStructureCapability DetectStructureCapability(
    const base::FilePath& profile_path) {
  const base::FilePath session_store =
      profile_path.AppendASCII(kZenSessionStoreName);
  struct stat file_stat;
  if (lstat(session_store.value().c_str(), &file_stat) != 0) {
    return ZenStructureCapability::kNotPresent;
  }
  base::ScopedFD fd = OpenSafeRegularFile(session_store, &file_stat);
  if (!fd.is_valid() || file_stat.st_size < 0 ||
      file_stat.st_size < static_cast<int64_t>(kMozLz4Header.size()) ||
      file_stat.st_size > kMaxSessionStoreBytes) {
    return ZenStructureCapability::kUnsafeOrOversized;
  }
  std::array<uint8_t, kMozLz4Header.size()> header{};
  const ssize_t bytes =
      HANDLE_EINTR(read(fd.get(), header.data(), header.size()));
  if (bytes != static_cast<ssize_t>(header.size())) {
    return ZenStructureCapability::kUnsafeOrOversized;
  }
  return header == kMozLz4Header ? ZenStructureCapability::kMozLz4Candidate
                                 : ZenStructureCapability::kUnsupportedHeader;
}

uint16_t DetectStandardServices(const base::FilePath& profile_path) {
  uint16_t services = user_data_importer::NONE;
  if (IsSafeRegularFile(profile_path.AppendASCII("places.sqlite"))) {
    services |= user_data_importer::HISTORY | user_data_importer::FAVORITES;
  }
  if (IsSafeRegularFile(profile_path.AppendASCII("formhistory.sqlite"))) {
    services |= user_data_importer::AUTOFILL_FORM_DATA;
  }
  return services;
}

std::optional<int> ReadCompatibleFirefoxMajorVersion(
    const base::FilePath& profile_path) {
  std::string content;
  if (!ReadSafeRegularFileWithMaxSize(
          profile_path.AppendASCII("compatibility.ini"),
          kMaxCompatibilityIniBytes, &content)) {
    return std::nullopt;
  }
  for (const std::string& line : base::SplitString(
           content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    constexpr std::string_view kPrefix = "LastVersion=";
    if (!line.starts_with(kPrefix)) {
      continue;
    }
    const std::string version = line.substr(kPrefix.size());
    size_t digit_count = 0;
    while (digit_count < version.size() &&
           std::isdigit(static_cast<unsigned char>(version[digit_count]))) {
      ++digit_count;
    }
    int major_version = 0;
    if (digit_count == 0 ||
        !base::StringToInt(version.substr(0, digit_count), &major_version)) {
      return std::nullopt;
    }
    return major_version;
  }
  return std::nullopt;
}

base::FilePath GetZenDataRoot() {
  base::FilePath app_data;
  if (!base::PathService::Get(base::DIR_APP_DATA, &app_data)) {
    return base::FilePath();
  }
  return app_data.AppendASCII("zen");
}

}  // namespace

std::vector<ZenProfileDetail> DiscoverZenProfilesAtRoot(
    const base::FilePath& zen_data_root) {
  std::vector<ZenProfileDetail> profiles;
  if (zen_data_root.empty() || !base::DirectoryExists(zen_data_root) ||
      base::IsLink(zen_data_root)) {
    return profiles;
  }

  const base::FilePath absolute_root =
      base::MakeAbsoluteFilePath(zen_data_root);
  const base::FilePath profiles_ini = absolute_root.AppendASCII("profiles.ini");
  if (absolute_root.empty()) {
    return profiles;
  }

  std::string content;
  if (!ReadSafeRegularFileWithMaxSize(profiles_ini, kMaxProfilesIniBytes,
                                      &content)) {
    return profiles;
  }
  DictionaryValueINIParser parser;
  parser.Parse(content);

  for (int index = 0; index < kMaxProfiles; ++index) {
    const std::string section = base::StringPrintf("Profile%d", index);
    if (!parser.root().Find(section)) {
      continue;
    }
    const std::optional<base::FilePath> profile_path =
        ResolveProfilePath(absolute_root, parser.root(), section);
    if (!profile_path) {
      continue;
    }

    ZenProfileDetail detail;
    detail.path = *profile_path;
    if (const std::string* name =
            parser.root().FindStringByDottedPath(section + ".Name")) {
      detail.name = base::UTF8ToUTF16(*name);
    }
    detail.services_supported = DetectStandardServices(detail.path);
    detail.structure_capability = DetectStructureCapability(detail.path);
    if (detail.services_supported != user_data_importer::NONE ||
        detail.structure_capability != ZenStructureCapability::kNotPresent) {
      profiles.push_back(std::move(detail));
    }
  }
  if (profiles.size() == 1u) {
    profiles.front().name.clear();
  }
  return profiles;
}

ZenImportAvailability AppendZenSourceProfiles(
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  if (!profiles) {
    return ZenImportAvailability::kNotInstalled;
  }
  return internal::AppendZenSourceProfilesForApplication(
      InspectDefaultZenApplication(), GetZenDataRoot(), locale, profiles);
}

namespace internal {

ZenImportAvailability AppendZenSourceProfilesForApplication(
    const ZenApplicationState& application,
    const base::FilePath& zen_data_root,
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  if (!profiles) {
    return ZenImportAvailability::kNotInstalled;
  }
  const ZenImportAvailability availability =
      GetZenImportAvailability(application);
  if (availability != ZenImportAvailability::kAvailable) {
    return availability;
  }
  const size_t first_zen_profile = profiles->size();
  AppendZenSourceProfilesAtRoot(zen_data_root, locale, profiles);
  const base::FilePath resource_path =
      application.bundle_path.AppendASCII("Contents/Resources");
  for (size_t index = first_zen_profile; index < profiles->size(); ++index) {
    (*profiles)[index].app_path = resource_path;
  }
  return availability;
}

}  // namespace internal

void AppendZenSourceProfilesAtRoot(
    const base::FilePath& zen_data_root,
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  if (!profiles) {
    return;
  }
  for (const ZenProfileDetail& detail :
       DiscoverZenProfilesAtRoot(zen_data_root)) {
    if (detail.services_supported == user_data_importer::NONE) {
      continue;
    }
    const std::optional<int> firefox_major_version =
        ReadCompatibleFirefoxMajorVersion(detail.path);
    if (!firefox_major_version || *firefox_major_version < 48) {
      continue;
    }
    user_data_importer::SourceProfile source;
    source.importer_name = u"Zen";
    source.profile = detail.name;
    source.importer_type = user_data_importer::TYPE_FIREFOX;
    source.source_path = detail.path;
    source.services_supported = detail.services_supported;
    source.locale = locale;
    profiles->push_back(std::move(source));
  }
}

}  // namespace ahoi::importer::zen
