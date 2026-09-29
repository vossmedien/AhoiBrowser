// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_recovery.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kBackupManifestVersion = 1;
constexpr int64_t kMaxManifestBytes = 1024 * 1024;
constexpr size_t kMaxManifestFiles = kMaxBrowserProfileCount * 10 + 4;
constexpr char kManifestFilename[] = "manifest.json";
constexpr char kAhoiDatabaseBackupName[] = "Ahoi-Tab-Tree.sqlite";

struct ManifestFile {
  std::string role;
  std::string source_path;
  std::string backup_name;
  bool present = false;
  int64_t bytes = 0;
  std::string sha256;
};

bool IsLowerSha256(const std::string& value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char character) {
           return base::IsHexDigit(character) &&
                  (base::IsAsciiDigit(character) ||
                   base::IsAsciiLower(character));
         });
}

bool IsSafeBackupIdentifier(std::string_view value) {
  return value.size() >= 16 && value.size() <= 64 &&
         std::ranges::all_of(value, [](char character) {
           return base::IsAsciiDigit(character) ||
                  (character >= 'a' && character <= 'f') || character == '-';
         });
}

bool IsSafeBackupName(std::string_view value) {
  const base::FilePath path = base::FilePath::FromUTF8Unsafe(value);
  return !value.empty() && value.size() <= 160 && !path.IsAbsolute() &&
         !path.ReferencesParent() && path.BaseName() == path &&
         (base::StartsWith(value, "Arc-") || base::StartsWith(value, "Ahoi-"));
}

bool IsSafeManifestSourcePath(std::string_view value) {
  const base::FilePath path = base::FilePath::FromUTF8Unsafe(value);
  return !value.empty() && value.size() <= 1024 && !path.IsAbsolute() &&
         !path.ReferencesParent() &&
         (base::StartsWith(value, "Arc/") ||
          base::StartsWith(value, "AhoiProfile/"));
}

bool IsOwnerOnlyDirectory(const base::FilePath& path) {
  struct stat state;
  return lstat(path.value().c_str(), &state) == 0 && S_ISDIR(state.st_mode) &&
         state.st_uid == getuid() && (state.st_mode & 0077) == 0;
}

base::File OpenOwnerOnlyFile(const base::FilePath& path,
                             int64_t maximum_bytes) {
  base::ScopedFD descriptor(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat state;
  if (!descriptor.is_valid() || fstat(descriptor.get(), &state) != 0 ||
      !S_ISREG(state.st_mode) || state.st_uid != getuid() ||
      state.st_nlink != 1 || (state.st_mode & 0077) != 0 || state.st_size < 0 ||
      state.st_size > maximum_bytes) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  return base::File(std::move(descriptor));
}

bool ReadAndHashFile(const base::FilePath& path,
                     int64_t maximum_bytes,
                     std::string* contents,
                     std::string* sha256) {
  base::File file = OpenOwnerOnlyFile(path, maximum_bytes);
  struct stat links_before;
  base::File::Info before;
  if (!file.IsValid() || fstat(file.GetPlatformFile(), &links_before) != 0 ||
      links_before.st_nlink != 1 || !file.GetInfo(&before) || before.size < 0) {
    return false;
  }
  contents->resize(static_cast<size_t>(before.size));
  size_t offset = 0;
  while (offset < contents->size()) {
    const std::optional<size_t> read_size = file.ReadAtCurrentPos(
        base::as_writable_byte_span(*contents).subspan(offset));
    if (!read_size.has_value() || *read_size == 0) {
      return false;
    }
    offset += *read_size;
  }
  struct stat links_after;
  base::File::Info after;
  if (fstat(file.GetPlatformFile(), &links_after) != 0 ||
      links_after.st_nlink != 1 || !file.GetInfo(&after) ||
      before.size != after.size ||
      before.last_modified != after.last_modified ||
      before.creation_time != after.creation_time) {
    return false;
  }
  *sha256 = base::HexEncodeLower(crypto::hash::Sha256(*contents));
  return true;
}

bool HashOpenedOwnerOnlyFile(base::File* file,
                             int64_t expected_bytes,
                             std::string* sha256) {
  struct stat links_before;
  base::File::Info before;
  if (!file || !file->IsValid() || file->Seek(base::File::FROM_BEGIN, 0) != 0 ||
      fstat(file->GetPlatformFile(), &links_before) != 0 ||
      links_before.st_nlink != 1 || !file->GetInfo(&before) ||
      before.size != expected_bytes) {
    return false;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest{};
  if (!crypto::hash::HashFile(crypto::hash::kSha256, file, digest)) {
    return false;
  }
  struct stat links_after;
  base::File::Info after;
  if (fstat(file->GetPlatformFile(), &links_after) != 0 ||
      links_after.st_nlink != 1 || !file->GetInfo(&after) ||
      before.size != after.size ||
      before.last_modified != after.last_modified ||
      before.creation_time != after.creation_time) {
    return false;
  }
  *sha256 = base::HexEncodeLower(digest);
  return true;
}

bool HashOwnerOnlyFile(const base::FilePath& path,
                       int64_t expected_bytes,
                       std::string* sha256) {
  base::File file = OpenOwnerOnlyFile(path, expected_bytes);
  return HashOpenedOwnerOnlyFile(&file, expected_bytes, sha256);
}

bool VerifyPayload(const base::FilePath& backup_directory,
                   const ManifestFile& file) {
  if (!file.present) {
    return !base::PathExists(backup_directory.AppendASCII(file.backup_name));
  }
  std::string digest;
  return HashOwnerOnlyFile(backup_directory.AppendASCII(file.backup_name),
                           file.bytes, &digest) &&
         digest == file.sha256;
}

std::optional<ManifestFile> ParseManifestFile(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  const std::string* role = dict ? dict->FindString("role") : nullptr;
  const std::string* source_path =
      dict ? dict->FindString("source_path") : nullptr;
  const std::string* backup_name =
      dict ? dict->FindString("backup_name") : nullptr;
  const std::optional<bool> present =
      dict ? dict->FindBool("present") : std::nullopt;
  if (!dict || !role || role->empty() || role->size() > 160 || !source_path ||
      !IsSafeManifestSourcePath(*source_path) || !backup_name ||
      !IsSafeBackupName(*backup_name) || !present.has_value()) {
    return std::nullopt;
  }
  ManifestFile file{.role = *role,
                    .source_path = *source_path,
                    .backup_name = *backup_name,
                    .present = *present};
  if (!file.present) {
    return file;
  }
  const std::string* bytes = dict->FindString("bytes");
  const std::string* sha256 = dict->FindString("sha256");
  if (!bytes || !base::StringToInt64(*bytes, &file.bytes) || file.bytes < 0 ||
      !sha256 || !IsLowerSha256(*sha256)) {
    return std::nullopt;
  }
  file.sha256 = *sha256;
  return file;
}

bool CopyVerifiedPayload(const base::FilePath& source,
                         const ManifestFile& expected,
                         const base::FilePath& destination) {
  base::File source_file = OpenOwnerOnlyFile(source, expected.bytes);
  std::string source_digest;
  if (!HashOpenedOwnerOnlyFile(&source_file, expected.bytes, &source_digest) ||
      source_digest != expected.sha256 ||
      source_file.Seek(base::File::FROM_BEGIN, 0) != 0 ||
      base::PathExists(destination)) {
    return false;
  }
  base::File destination_file(destination, base::File::FLAG_CREATE |
                                               base::File::FLAG_WRITE |
                                               base::File::FLAG_NO_FOLLOW);
  if (!destination_file.IsValid() ||
      !base::SetPosixFilePermissions(destination, 0600) ||
      !base::CopyFileContents(source_file, destination_file) ||
      !destination_file.Flush()) {
    return false;
  }
  destination_file.Close();
  std::string source_after_copy;
  std::string destination_digest;
  return HashOpenedOwnerOnlyFile(&source_file, expected.bytes,
                                 &source_after_copy) &&
         HashOwnerOnlyFile(destination, expected.bytes, &destination_digest) &&
         source_after_copy == expected.sha256 &&
         destination_digest == source_after_copy;
}

}  // namespace

ArcImportBackupRecoveryResult VerifyAndLoadArcImportBackup(
    const base::FilePath& profile_path,
    const std::string& backup_identifier,
    const std::string& expected_manifest_sha256,
    const std::string& expected_snapshot_sha256) {
  ArcImportBackupRecoveryResult result;
  if (profile_path.empty() || !profile_path.IsAbsolute() ||
      profile_path.ReferencesParent() ||
      !IsSafeBackupIdentifier(backup_identifier) ||
      !IsLowerSha256(expected_manifest_sha256) ||
      !IsLowerSha256(expected_snapshot_sha256) ||
      !base::StartsWith(backup_identifier,
                        expected_snapshot_sha256.substr(0, 12) + "-")) {
    return result;
  }
  const base::FilePath ahoi_directory = profile_path.AppendASCII("Ahoi");
  const base::FilePath backup_root =
      ahoi_directory.AppendASCII("Arc Import Backups");
  const base::FilePath backup_directory =
      backup_root.AppendASCII(backup_identifier);
  if (backup_directory.DirName() != backup_root ||
      !IsOwnerOnlyDirectory(ahoi_directory) ||
      !IsOwnerOnlyDirectory(backup_root) ||
      !IsOwnerOnlyDirectory(backup_directory)) {
    return result;
  }

  std::string manifest_json;
  std::string manifest_digest;
  if (!ReadAndHashFile(backup_directory.AppendASCII(kManifestFilename),
                       kMaxManifestBytes, &manifest_json, &manifest_digest) ||
      manifest_digest != expected_manifest_sha256) {
    return result;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(manifest_json, base::JSON_PARSE_RFC);
  const base::DictValue* manifest =
      parsed.has_value() ? parsed->GetIfDict() : nullptr;
  const std::optional<int> version =
      manifest ? manifest->FindInt("version") : std::nullopt;
  const std::string* snapshot_hash =
      manifest ? manifest->FindString("snapshot_sha256") : nullptr;
  const base::ListValue* files =
      manifest ? manifest->FindList("files") : nullptr;
  if (!version.has_value() || *version != kBackupManifestVersion ||
      !snapshot_hash || *snapshot_hash != expected_snapshot_sha256 || !files ||
      files->empty() || files->size() > kMaxManifestFiles) {
    return result;
  }

  std::map<std::string, ManifestFile> by_name;
  std::set<std::string> roles;
  for (const base::Value& value : *files) {
    std::optional<ManifestFile> file = ParseManifestFile(value);
    if (!file || !roles.insert(file->role).second ||
        !by_name.emplace(file->backup_name, *file).second ||
        !VerifyPayload(backup_directory, *file)) {
      return result;
    }
  }
  const auto database = by_name.find(kAhoiDatabaseBackupName);
  if (database == by_name.end() || database->second.role != "ahoi_tab_tree" ||
      database->second.source_path != "AhoiProfile/Ahoi Tab Tree" ||
      !database->second.present) {
    return result;
  }

  std::set<std::string> expected_files = {kManifestFilename};
  for (const auto& [name, file] : by_name) {
    if (file.present) {
      expected_files.insert(name);
    }
  }
  base::FileEnumerator enumerator(backup_directory, false,
                                  base::FileEnumerator::FILES |
                                      base::FileEnumerator::DIRECTORIES |
                                      base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    const std::string name = path.BaseName().AsUTF8Unsafe();
    if (!expected_files.erase(name)) {
      return result;
    }
  }
  if (!expected_files.empty()) {
    return result;
  }

  base::ScopedTempDir recovery_directory;
  if (!recovery_directory.CreateUniqueTempDir()) {
    return result;
  }
  const base::FilePath recovery_database =
      recovery_directory.GetPath().AppendASCII(kTabTreeDatabaseFilename);
  if (!CopyVerifiedPayload(
          backup_directory.AppendASCII(database->second.backup_name),
          database->second, recovery_database)) {
    return result;
  }
  for (std::string_view suffix : {"-wal", "-shm"}) {
    const std::string backup_name =
        std::string(kAhoiDatabaseBackupName) + std::string(suffix);
    const auto sidecar = by_name.find(backup_name);
    if (sidecar == by_name.end()) {
      return result;
    }
    const std::string expected_role =
        suffix == "-wal" ? "ahoi_tab_tree_wal" : "ahoi_tab_tree_shm";
    if (sidecar->second.role != expected_role) {
      return result;
    }
    if (!sidecar->second.present) {
      continue;
    }
    if (suffix == "-shm") {
      continue;
    }
    const base::FilePath destination =
        base::FilePath(recovery_database.value() +
                       base::FilePath::FromUTF8Unsafe(suffix).value());
    if (!CopyVerifiedPayload(
            backup_directory.AppendASCII(sidecar->second.backup_name),
            sidecar->second, destination)) {
      return result;
    }
  }

  tab_tree::TabTreeStore store;
  tab_tree::TabTreeSnapshot previous;
  if (!store.Initialize(recovery_database) ||
      store.ExportSnapshot(&previous) != tab_tree::TabTreeStore::Result::kOk) {
    return result;
  }
  result.status = ArcImportStatus::kOk;
  result.previous_tree = std::move(previous);
  return result;
}

}  // namespace ahoi::importer::arc
