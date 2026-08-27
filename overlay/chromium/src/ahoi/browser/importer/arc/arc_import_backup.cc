// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_backup.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kBackupManifestVersion = 1;
constexpr base::FilePath::CharType kAhoiDirectory[] = FILE_PATH_LITERAL("Ahoi");
constexpr base::FilePath::CharType kBackupRootDirectory[] =
    FILE_PATH_LITERAL("Arc Import Backups");
constexpr base::FilePath::CharType kManifestFilename[] =
    FILE_PATH_LITERAL("manifest.json");
constexpr base::FilePath::CharType kTemporaryManifestFilename[] =
    FILE_PATH_LITERAL("manifest.json.tmp");

struct BackupFileSpec {
  std::string role;
  base::FilePath source;
  std::string manifest_source_path;
  base::FilePath::StringType destination_name;
  bool required;
  bool is_arc_sidebar = false;
};

bool IsLowerSha256(const std::string& value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char character) {
           return base::IsHexDigit(character) &&
                  (base::IsAsciiDigit(character) ||
                   base::IsAsciiLower(character));
         });
}

bool HasSafeSelectedProfiles(const ArcSource& source) {
  const base::FilePath user_data =
      source.arc_root.Append(FILE_PATH_LITERAL("User Data"));
  std::set<std::string> names;
  for (const ArcBrowserProfile& profile : source.browser_profiles) {
    if (profile.directory_name.empty() ||
        !names.insert(profile.directory_name).second ||
        profile.path.DirName() != user_data ||
        profile.path.BaseName().MaybeAsASCII() != profile.directory_name ||
        profile.path.ReferencesParent() || base::IsLink(profile.path) ||
        !base::DirectoryExists(profile.path)) {
      return false;
    }
  }
  return true;
}

bool IsSafeManifestSourcePath(std::string_view value) {
  const base::FilePath path = base::FilePath::FromUTF8Unsafe(value);
  return !value.empty() && !path.IsAbsolute() && !path.ReferencesParent() &&
         (base::StartsWith(value, "Arc/") ||
          base::StartsWith(value, "AhoiProfile/"));
}

bool HashRegularFile(const base::FilePath& path,
                     std::string* hash,
                     int64_t* size,
                     int64_t* modified_unix_ms) {
  if (base::IsLink(path)) {
    return false;
  }
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                            base::File::FLAG_NO_FOLLOW);
  base::File::Info info;
  if (!file.IsValid() || !file.GetInfo(&info) || info.is_directory ||
      info.is_symbolic_link || info.size < 0) {
    return false;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest{};
  if (!crypto::hash::HashFile(crypto::hash::kSha256, &file, digest)) {
    return false;
  }
  *hash = base::HexEncodeLower(digest);
  *size = info.size;
  *modified_unix_ms = info.last_modified.InMillisecondsSinceUnixEpoch();
  return true;
}

bool CopyAndVerify(const BackupFileSpec& spec,
                   const base::FilePath& backup_directory,
                   base::Value::List* manifest_files,
                   const std::string& expected_snapshot_token) {
  if (!IsSafeManifestSourcePath(spec.manifest_source_path)) {
    return false;
  }
  base::Value::Dict entry;
  entry.Set("role", spec.role);
  entry.Set("source_path", spec.manifest_source_path);
  if (!base::PathExists(spec.source)) {
    if (spec.required) {
      return false;
    }
    entry.Set("present", false);
    manifest_files->Append(std::move(entry));
    return true;
  }
  if (base::IsLink(spec.source)) {
    return false;
  }

  std::string source_hash;
  int64_t source_size = 0;
  int64_t source_modified_unix_ms = 0;
  if (!HashRegularFile(spec.source, &source_hash, &source_size,
                       &source_modified_unix_ms) ||
      (spec.is_arc_sidebar && source_hash != expected_snapshot_token)) {
    return false;
  }

  const base::FilePath destination =
      backup_directory.Append(spec.destination_name);
  if (base::PathExists(destination) ||
      !base::CopyFile(spec.source, destination) ||
      !base::SetPosixFilePermissions(destination, 0600)) {
    return false;
  }
  std::string destination_hash;
  int64_t destination_size = 0;
  int64_t destination_modified_unix_ms = 0;
  if (!HashRegularFile(destination, &destination_hash, &destination_size,
                       &destination_modified_unix_ms) ||
      source_hash != destination_hash || source_size != destination_size) {
    return false;
  }

  entry.Set("present", true);
  entry.Set("bytes", base::NumberToString(source_size));
  entry.Set("modified_unix_ms", base::NumberToString(source_modified_unix_ms));
  entry.Set("sha256", source_hash);
  manifest_files->Append(std::move(entry));
  return true;
}

void AddDatabaseSpecs(std::vector<BackupFileSpec>* specs,
                      std::string role_prefix,
                      const base::FilePath& source,
                      std::string manifest_source_path,
                      const base::FilePath::StringType& destination_name) {
  specs->push_back(
      {role_prefix, source, manifest_source_path, destination_name, false});
  specs->push_back({base::StrCat({role_prefix, "_wal"}),
                    base::FilePath(source.value() + FILE_PATH_LITERAL("-wal")),
                    base::StrCat({manifest_source_path, "-wal"}),
                    destination_name + FILE_PATH_LITERAL("-wal"), false});
  specs->push_back({base::StrCat({role_prefix, "_shm"}),
                    base::FilePath(source.value() + FILE_PATH_LITERAL("-shm")),
                    base::StrCat({manifest_source_path, "-shm"}),
                    destination_name + FILE_PATH_LITERAL("-shm"), false});
}

}  // namespace

ArcImportBackupResult CreateArcImportBackup(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token) {
  ArcImportBackupResult result;
  if (IsArcApplicationRunning() || AreArcProfileFilesOpen(arc_source)) {
    result.status = ArcImportStatus::kSourceInUse;
    return result;
  }
  if (ahoi_profile_path.empty() || !ahoi_profile_path.IsAbsolute() ||
      ahoi_profile_path.ReferencesParent() ||
      arc_source.browser_profiles.empty() ||
      base::IsLink(arc_source.arc_root) ||
      base::IsLink(
          arc_source.arc_root.Append(FILE_PATH_LITERAL("User Data"))) ||
      !HasSafeSelectedProfiles(arc_source) ||
      !IsLowerSha256(expected_snapshot_token)) {
    result.status = ArcImportStatus::kInvalidPath;
    return result;
  }

  const base::FilePath ahoi_directory =
      ahoi_profile_path.Append(kAhoiDirectory);
  const base::FilePath backup_root =
      ahoi_directory.Append(kBackupRootDirectory);
  if (base::IsLink(ahoi_directory) || base::IsLink(backup_root) ||
      (!base::DirectoryExists(backup_root) &&
       !base::CreateDirectory(backup_root)) ||
      !base::SetPosixFilePermissions(ahoi_directory, 0700) ||
      !base::SetPosixFilePermissions(backup_root, 0700)) {
    return result;
  }

  const std::string directory_name =
      expected_snapshot_token.substr(0, 12) + "-" +
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath backup_directory =
      backup_root.AppendASCII(directory_name);
  if (base::PathExists(backup_directory) ||
      !base::CreateDirectory(backup_directory) ||
      !base::SetPosixFilePermissions(backup_directory, 0700)) {
    return result;
  }

  std::vector<BackupFileSpec> specs = {
      {"arc_sidebar", arc_source.sidebar_file, "Arc/StorableSidebar.json",
       FILE_PATH_LITERAL("Arc-StorableSidebar.json"), true, true},
  };
  for (const ArcBrowserProfile& arc_profile : arc_source.browser_profiles) {
    std::string slug = base::ToLowerASCII(arc_profile.directory_name);
    std::ranges::replace(slug, ' ', '_');
    const std::string manifest_profile_prefix =
        base::StrCat({"Arc/User Data/", arc_profile.directory_name, "/"});
    specs.push_back({base::StrCat({"arc_", slug, "_bookmarks"}),
                     arc_profile.path.AppendASCII("Bookmarks"),
                     base::StrCat({manifest_profile_prefix, "Bookmarks"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", slug, "-Bookmarks.json"}))
                         .value(),
                     false});
    AddDatabaseSpecs(&specs, base::StrCat({"arc_", slug, "_history"}),
                     arc_profile.path.AppendASCII("History"),
                     base::StrCat({manifest_profile_prefix, "History"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", slug, "-History.sqlite"}))
                         .value());
    AddDatabaseSpecs(&specs, base::StrCat({"arc_", slug, "_favicons"}),
                     arc_profile.path.AppendASCII("Favicons"),
                     base::StrCat({manifest_profile_prefix, "Favicons"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", slug, "-Favicons.sqlite"}))
                         .value());
    AddDatabaseSpecs(&specs, base::StrCat({"arc_", slug, "_web_data"}),
                     arc_profile.path.AppendASCII("Web Data"),
                     base::StrCat({manifest_profile_prefix, "Web Data"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", slug, "-Web-Data.sqlite"}))
                         .value());
  }
  AddDatabaseSpecs(&specs, "ahoi_tab_tree",
                   ahoi_profile_path.AppendASCII(kTabTreeDatabaseFilename),
                   base::StrCat({"AhoiProfile/", kTabTreeDatabaseFilename}),
                   FILE_PATH_LITERAL("Ahoi-Tab-Tree.sqlite"));

  base::Value::List manifest_files;
  bool success = true;
  for (const BackupFileSpec& spec : specs) {
    if (!CopyAndVerify(spec, backup_directory, &manifest_files,
                       expected_snapshot_token)) {
      success = false;
      break;
    }
  }
  if (success &&
      (IsArcApplicationRunning() || AreArcProfileFilesOpen(arc_source))) {
    result.status = ArcImportStatus::kSourceInUse;
    success = false;
  }

  base::Value::Dict manifest;
  manifest.Set("version", kBackupManifestVersion);
  manifest.Set("snapshot_sha256", expected_snapshot_token);
  manifest.Set("files", std::move(manifest_files));
  std::string json;
  const base::FilePath manifest_path =
      backup_directory.Append(kManifestFilename);
  const base::FilePath temporary_manifest =
      backup_directory.Append(kTemporaryManifestFilename);
  success = success && base::JSONWriter::Write(manifest, &json) &&
            base::WriteFile(temporary_manifest, json) &&
            base::SetPosixFilePermissions(temporary_manifest, 0600);
  base::File::Error replace_error = base::File::FILE_OK;
  success =
      success &&
      base::ReplaceFile(temporary_manifest, manifest_path, &replace_error) &&
      base::SetPosixFilePermissions(manifest_path, 0600);
  if (!success) {
    base::DeleteFile(temporary_manifest);
    base::DeletePathRecursively(backup_directory);
    return result;
  }

  result.status = ArcImportStatus::kOk;
  result.backup_directory = backup_directory;
  return result;
}

}  // namespace ahoi::importer::arc
