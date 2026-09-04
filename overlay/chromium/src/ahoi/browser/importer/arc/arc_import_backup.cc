// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_backup.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_journal.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kBackupManifestVersion = 1;
constexpr int64_t kMaxManifestBytes = 1024 * 1024;
constexpr size_t kMaxManifestFiles = kMaxBrowserProfileCount * 10 + 4;
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

struct BackupFileState {
  bool present = false;
  std::string sha256;
  int64_t size = 0;
  int64_t modified_unix_ms = 0;
  int64_t created_unix_ms = 0;
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

bool IsSafeBackupName(std::string_view value) {
  const base::FilePath path = base::FilePath::FromUTF8Unsafe(value);
  return !value.empty() && value.size() <= 160 && !path.IsAbsolute() &&
         !path.ReferencesParent() && path.BaseName() == path &&
         (base::StartsWith(value, "Arc-") || base::StartsWith(value, "Ahoi-"));
}

bool BackupFileStatesMatch(const BackupFileState& left,
                           const BackupFileState& right) {
  return left.present == right.present && left.sha256 == right.sha256 &&
         left.size == right.size &&
         left.modified_unix_ms == right.modified_unix_ms &&
         left.created_unix_ms == right.created_unix_ms;
}

base::File OpenSafeRegularFile(const base::FilePath& path) {
  base::ScopedFD fd(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat file_stat;
  if (!fd.is_valid() || fstat(fd.get(), &file_stat) != 0 ||
      !S_ISREG(file_stat.st_mode) || file_stat.st_nlink != 1) {
    return base::File(base::File::FILE_ERROR_NOT_A_FILE);
  }
  return base::File(std::move(fd));
}

bool HashOpenedRegularFile(base::File* file, BackupFileState* state) {
  struct stat links_before;
  base::File::Info info_before;
  if (!file || !state || !file->IsValid() ||
      file->Seek(base::File::FROM_BEGIN, 0) != 0 ||
      fstat(file->GetPlatformFile(), &links_before) != 0 ||
      links_before.st_nlink != 1 || !file->GetInfo(&info_before) ||
      info_before.is_directory || info_before.is_symbolic_link ||
      info_before.size < 0) {
    return false;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest{};
  if (!crypto::hash::HashFile(crypto::hash::kSha256, file, digest)) {
    return false;
  }
  struct stat links_after;
  base::File::Info info_after;
  if (fstat(file->GetPlatformFile(), &links_after) != 0 ||
      links_after.st_nlink != 1 || !file->GetInfo(&info_after) ||
      info_after.is_directory || info_after.is_symbolic_link ||
      info_after.size < 0 || info_before.size != info_after.size ||
      info_before.last_modified != info_after.last_modified ||
      info_before.creation_time != info_after.creation_time) {
    return false;
  }
  state->present = true;
  state->sha256 = base::HexEncodeLower(digest);
  state->size = info_after.size;
  state->modified_unix_ms =
      info_after.last_modified.InMillisecondsSinceUnixEpoch();
  state->created_unix_ms =
      info_after.creation_time.InMillisecondsSinceUnixEpoch();
  return true;
}

bool HashRegularFile(const base::FilePath& path, BackupFileState* state) {
  base::File file = OpenSafeRegularFile(path);
  return HashOpenedRegularFile(&file, state);
}

bool IsOwnerOnlyOpenedRegularFile(base::File* file) {
  struct stat state;
  return file && file->IsValid() &&
         fstat(file->GetPlatformFile(), &state) == 0 &&
         S_ISREG(state.st_mode) && state.st_nlink == 1 &&
         state.st_uid == getuid() && (state.st_mode & 0077) == 0;
}

bool ReadStableOwnerOnlyFile(base::File* file,
                             int64_t max_bytes,
                             std::string* contents) {
  if (!contents || max_bytes < 0 || !IsOwnerOnlyOpenedRegularFile(file)) {
    return false;
  }
  BackupFileState state_before;
  if (!HashOpenedRegularFile(file, &state_before) ||
      state_before.size > max_bytes) {
    return false;
  }
  contents->resize(static_cast<size_t>(state_before.size));
  if (!file->ReadAndCheck(0, base::as_writable_byte_span(*contents))) {
    return false;
  }
  BackupFileState state_after;
  return HashOpenedRegularFile(file, &state_after) &&
         BackupFileStatesMatch(state_before, state_after) &&
         IsOwnerOnlyOpenedRegularFile(file);
}

bool IsPathMissingNoFollow(const base::FilePath& path) {
  struct stat state;
  if (lstat(path.value().c_str(), &state) == 0) {
    return false;
  }
  return errno == ENOENT;
}

bool FsyncDirectory(const base::FilePath& path) {
  base::ScopedFD descriptor(HANDLE_EINTR(
      open(path.value().c_str(),
           O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK)));
  struct stat state;
  return descriptor.is_valid() && fstat(descriptor.get(), &state) == 0 &&
         S_ISDIR(state.st_mode) && state.st_uid == getuid() &&
         HANDLE_EINTR(fsync(descriptor.get())) == 0;
}

bool CaptureBackupFileState(const BackupFileSpec& spec,
                            BackupFileState* state) {
  if (base::IsLink(spec.source)) {
    return false;
  }
  if (!base::PathExists(spec.source)) {
    if (spec.required) {
      return false;
    }
    *state = BackupFileState();
    return true;
  }
  return HashRegularFile(spec.source, state);
}

bool CopyAndVerify(const BackupFileSpec& spec,
                   const BackupFileState& expected,
                   const base::FilePath& backup_directory,
                   base::ListValue* manifest_files,
                   const std::string& expected_snapshot_token) {
  if (!IsSafeManifestSourcePath(spec.manifest_source_path)) {
    return false;
  }
  base::DictValue entry;
  entry.Set("role", spec.role);
  entry.Set("source_path", spec.manifest_source_path);
  entry.Set("backup_name",
            base::FilePath(spec.destination_name).AsUTF8Unsafe());
  if (!expected.present) {
    if (spec.required || base::IsLink(spec.source) ||
        base::PathExists(spec.source)) {
      return false;
    }
    entry.Set("present", false);
    manifest_files->Append(std::move(entry));
    return true;
  }
  if (base::IsLink(spec.source)) {
    return false;
  }

  BackupFileState current;
  if (!CaptureBackupFileState(spec, &current) ||
      !BackupFileStatesMatch(expected, current) ||
      (spec.is_arc_sidebar && expected.sha256 != expected_snapshot_token)) {
    return false;
  }

  const base::FilePath destination =
      backup_directory.Append(spec.destination_name);
  base::File source_file = OpenSafeRegularFile(spec.source);
  BackupFileState opened_source_state;
  if (!HashOpenedRegularFile(&source_file, &opened_source_state) ||
      !BackupFileStatesMatch(expected, opened_source_state) ||
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
    destination_file.Close();
    base::DeleteFile(destination);
    return false;
  }
  destination_file.Close();

  // Verify the exact opened inode once more after copying. A leaf swap cannot
  // redirect the copy because the source descriptor was opened with
  // O_NOFOLLOW; in-place mutation invalidates the generation as well.
  BackupFileState source_after_copy;
  if (!HashOpenedRegularFile(&source_file, &source_after_copy) ||
      !BackupFileStatesMatch(expected, source_after_copy) ||
      !base::SetPosixFilePermissions(destination, 0600)) {
    base::DeleteFile(destination);
    return false;
  }
  BackupFileState destination_state;
  if (!HashRegularFile(destination, &destination_state) ||
      expected.sha256 != destination_state.sha256 ||
      expected.size != destination_state.size) {
    return false;
  }

  entry.Set("present", true);
  entry.Set("bytes", base::NumberToString(expected.size));
  entry.Set("modified_unix_ms",
            base::NumberToString(expected.modified_unix_ms));
  entry.Set("sha256", expected.sha256);
  manifest_files->Append(std::move(entry));
  return true;
}

void AddDatabaseSpecs(std::vector<BackupFileSpec>* specs,
                      std::string role_prefix,
                      const base::FilePath& source,
                      std::string manifest_source_path,
                      const base::FilePath::StringType& destination_name,
                      bool primary_required = false) {
  specs->push_back({role_prefix, source, manifest_source_path, destination_name,
                    primary_required});
  specs->push_back({base::StrCat({role_prefix, "_wal"}),
                    base::FilePath(source.value() + FILE_PATH_LITERAL("-wal")),
                    base::StrCat({manifest_source_path, "-wal"}),
                    destination_name + FILE_PATH_LITERAL("-wal"), false});
  specs->push_back({base::StrCat({role_prefix, "_shm"}),
                    base::FilePath(source.value() + FILE_PATH_LITERAL("-shm")),
                    base::StrCat({manifest_source_path, "-shm"}),
                    destination_name + FILE_PATH_LITERAL("-shm"), false});
}

bool IsSafeBackupIdentifier(std::string_view value) {
  if (value.size() != 49 || value[12] != '-') {
    return false;
  }
  for (size_t index = 0; index < 12; ++index) {
    if (!base::IsAsciiDigit(value[index]) &&
        !(value[index] >= 'a' && value[index] <= 'f')) {
      return false;
    }
  }
  const base::Uuid uuid = base::Uuid::ParseLowercase(value.substr(13));
  return uuid.is_valid() && uuid.AsLowercaseString() == value.substr(13);
}

bool IsOwnerOnlyDirectory(const base::FilePath& path) {
  struct stat state;
  return lstat(path.value().c_str(), &state) == 0 && S_ISDIR(state.st_mode) &&
         !S_ISLNK(state.st_mode) && state.st_uid == getuid() &&
         (state.st_mode & 0077) == 0;
}

bool IsValidatedBackupDirectory(const base::FilePath& path) {
  if (!IsSafeBackupIdentifier(path.BaseName().MaybeAsASCII()) ||
      !IsOwnerOnlyDirectory(path)) {
    return false;
  }
  const std::string identifier = path.BaseName().MaybeAsASCII();
  const base::FilePath manifest = path.Append(kManifestFilename);
  base::File manifest_file = OpenSafeRegularFile(manifest);
  std::string manifest_json;
  if (!ReadStableOwnerOnlyFile(&manifest_file, kMaxManifestBytes,
                               &manifest_json)) {
    return false;
  }
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(manifest_json, base::JSON_PARSE_RFC);
  const base::DictValue* manifest_dict =
      parsed.has_value() ? parsed->GetIfDict() : nullptr;
  const std::optional<int> version =
      manifest_dict ? manifest_dict->FindInt("version") : std::nullopt;
  const std::string* manifest_identifier =
      manifest_dict ? manifest_dict->FindString("backup_identifier") : nullptr;
  const std::string* snapshot_hash =
      manifest_dict ? manifest_dict->FindString("snapshot_sha256") : nullptr;
  const base::ListValue* files =
      manifest_dict ? manifest_dict->FindList("files") : nullptr;
  if (!manifest_dict || manifest_dict->size() != 4u || !version.has_value() ||
      *version != kBackupManifestVersion || !manifest_identifier ||
      *manifest_identifier != identifier || !snapshot_hash ||
      !IsLowerSha256(*snapshot_hash) || !files || files->empty() ||
      files->size() > kMaxManifestFiles) {
    return false;
  }

  std::set<std::string> expected_entries = {"manifest.json"};
  std::set<std::string> backup_names;
  std::set<std::string> roles;
  std::set<std::string> source_paths;
  bool has_sidebar = false;
  bool has_ahoi_tree = false;
  for (const base::Value& value : *files) {
    const base::DictValue* entry = value.GetIfDict();
    const std::string* role = entry ? entry->FindString("role") : nullptr;
    const std::string* source_path =
        entry ? entry->FindString("source_path") : nullptr;
    const std::string* backup_name =
        entry ? entry->FindString("backup_name") : nullptr;
    const std::optional<bool> present =
        entry ? entry->FindBool("present") : std::nullopt;
    if (!entry || !role || role->empty() || role->size() > 160 ||
        !roles.insert(*role).second || !source_path ||
        source_path->size() > 1024 || !IsSafeManifestSourcePath(*source_path) ||
        !source_paths.insert(*source_path).second || !backup_name ||
        !IsSafeBackupName(*backup_name) ||
        !backup_names.insert(*backup_name).second || !present.has_value() ||
        entry->size() != (*present ? 7u : 4u)) {
      return false;
    }
    const base::FilePath payload = path.AppendASCII(*backup_name);
    if (*present) {
      const std::string* bytes = entry->FindString("bytes");
      const std::string* modified_unix_ms =
          entry->FindString("modified_unix_ms");
      const std::string* sha256 = entry->FindString("sha256");
      int64_t parsed_bytes = -1;
      int64_t parsed_modified_unix_ms = 0;
      if (!bytes || !base::StringToInt64(*bytes, &parsed_bytes) ||
          parsed_bytes < 0 || !modified_unix_ms ||
          !base::StringToInt64(*modified_unix_ms, &parsed_modified_unix_ms) ||
          !sha256 || !IsLowerSha256(*sha256) ||
          !expected_entries.insert(*backup_name).second) {
        return false;
      }
      base::File payload_file = OpenSafeRegularFile(payload);
      BackupFileState payload_state;
      if (!IsOwnerOnlyOpenedRegularFile(&payload_file) ||
          !HashOpenedRegularFile(&payload_file, &payload_state) ||
          !IsOwnerOnlyOpenedRegularFile(&payload_file) ||
          payload_state.size != parsed_bytes ||
          payload_state.sha256 != *sha256) {
        return false;
      }
    } else if (!IsPathMissingNoFollow(payload)) {
      return false;
    }
    if (*role == "arc_sidebar") {
      has_sidebar = *present && *source_path == "Arc/StorableSidebar.json" &&
                    *backup_name == "Arc-StorableSidebar.json";
    } else if (*role == "ahoi_tab_tree") {
      has_ahoi_tree = *present && *source_path == "AhoiProfile/Ahoi Tab Tree" &&
                      *backup_name == "Ahoi-Tab-Tree.sqlite";
    }
  }
  if (!has_sidebar || !has_ahoi_tree) {
    return false;
  }

  base::FileEnumerator entries(
      path, true,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS,
      base::FilePath::StringType(),
      base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  size_t entry_count = 0;
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    if (++entry_count > 512) {
      return false;
    }
    struct stat state;
    if (lstat(entry.value().c_str(), &state) != 0 || S_ISLNK(state.st_mode) ||
        state.st_uid != getuid() || (state.st_mode & 0077) != 0 ||
        (!S_ISREG(state.st_mode) && !S_ISDIR(state.st_mode)) ||
        (S_ISREG(state.st_mode) && state.st_nlink != 1)) {
      return false;
    }
    if (S_ISDIR(state.st_mode) ||
        !expected_entries.erase(entry.BaseName().AsUTF8Unsafe())) {
      return false;
    }
  }
  return entries.GetError() == base::File::FILE_OK && expected_entries.empty();
}

}  // namespace

namespace internal {

ArcImportStatus CheckArcImportBackupResources(
    const std::vector<int64_t>& present_file_sizes,
    int64_t free_disk_bytes,
    const ArcImportBackupLimits& limits,
    uint64_t* total_bytes) {
  if (!total_bytes || limits.max_file_count == 0 ||
      limits.minimum_free_headroom_bytes < 0 || free_disk_bytes < 0 ||
      present_file_sizes.size() > limits.max_file_count) {
    return ArcImportStatus::kBackupQuotaExceeded;
  }
  uint64_t total = 0;
  for (int64_t size : present_file_sizes) {
    if (size < 0 || static_cast<uint64_t>(size) > limits.max_total_bytes ||
        total > limits.max_total_bytes - static_cast<uint64_t>(size)) {
      return ArcImportStatus::kBackupQuotaExceeded;
    }
    total += static_cast<uint64_t>(size);
  }
  *total_bytes = total;
  const uint64_t headroom =
      static_cast<uint64_t>(limits.minimum_free_headroom_bytes);
  if (total > std::numeric_limits<uint64_t>::max() - headroom ||
      static_cast<uint64_t>(free_disk_bytes) < total + headroom) {
    return ArcImportStatus::kInsufficientDiskSpace;
  }
  return ArcImportStatus::kOk;
}

bool PruneArcImportBackupsForTesting(
    const base::FilePath& backup_root,
    const std::set<std::string>& protected_identifiers,
    size_t max_retained_backups) {
  if (!IsOwnerOnlyDirectory(backup_root)) {
    return false;
  }
  struct Candidate {
    base::FilePath path;
    std::string identifier;
    base::Time modified;
  };
  std::vector<Candidate> candidates;
  base::FileEnumerator directories(
      backup_root, false,
      base::FileEnumerator::DIRECTORIES | base::FileEnumerator::SHOW_SYM_LINKS,
      base::FilePath::StringType(),
      base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (base::FilePath path = directories.Next(); !path.empty();
       path = directories.Next()) {
    if (!IsValidatedBackupDirectory(path)) {
      continue;
    }
    base::File::Info info;
    if (!base::GetFileInfo(path, &info)) {
      return false;
    }
    candidates.push_back(
        {path, path.BaseName().MaybeAsASCII(), info.last_modified});
  }
  if (directories.GetError() != base::File::FILE_OK) {
    return false;
  }
  std::ranges::sort(candidates,
                    [](const Candidate& left, const Candidate& right) {
                      if (left.modified != right.modified) {
                        return left.modified > right.modified;
                      }
                      return left.identifier > right.identifier;
                    });
  size_t retained =
      std::ranges::count_if(candidates, [&](const Candidate& candidate) {
        return protected_identifiers.contains(candidate.identifier);
      });
  for (const Candidate& candidate : candidates) {
    if (protected_identifiers.contains(candidate.identifier)) {
      continue;
    }
    if (retained < max_retained_backups) {
      ++retained;
      continue;
    }
    // Revalidate immediately before deletion. A backup that changed after the
    // initial bounded enumeration is no longer importer-owned deletion input.
    if (!IsValidatedBackupDirectory(candidate.path) ||
        !base::DeletePathRecursively(candidate.path)) {
      return false;
    }
  }
  return true;
}

}  // namespace internal

namespace {

ArcImportBackupResult CreateArcImportBackupWithSourceUseCheck(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token,
    const internal::ArcSourceUseCheck& source_use_check) {
  ArcImportBackupResult result;
  if (!source_use_check || source_use_check.Run(arc_source)) {
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

  std::vector<BackupFileSpec> specs = {
      {"arc_sidebar", arc_source.sidebar_file, "Arc/StorableSidebar.json",
       FILE_PATH_LITERAL("Arc-StorableSidebar.json"), true, true},
  };
  for (const ArcBrowserProfile& arc_profile : arc_source.browser_profiles) {
    const std::string profile_key =
        base::HexEncodeLower(crypto::hash::Sha256(arc_profile.directory_name));
    const std::string manifest_profile_prefix =
        base::StrCat({"Arc/User Data/", arc_profile.directory_name, "/"});
    specs.push_back({base::StrCat({"arc_", profile_key, "_bookmarks"}),
                     arc_profile.path.AppendASCII("Bookmarks"),
                     base::StrCat({manifest_profile_prefix, "Bookmarks"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", profile_key, "-Bookmarks.json"}))
                         .value(),
                     false});
    AddDatabaseSpecs(&specs, base::StrCat({"arc_", profile_key, "_history"}),
                     arc_profile.path.AppendASCII("History"),
                     base::StrCat({manifest_profile_prefix, "History"}),
                     base::FilePath::FromUTF8Unsafe(
                         base::StrCat({"Arc-", profile_key, "-History.sqlite"}))
                         .value());
    AddDatabaseSpecs(
        &specs, base::StrCat({"arc_", profile_key, "_favicons"}),
        arc_profile.path.AppendASCII("Favicons"),
        base::StrCat({manifest_profile_prefix, "Favicons"}),
        base::FilePath::FromUTF8Unsafe(
            base::StrCat({"Arc-", profile_key, "-Favicons.sqlite"}))
            .value());
    AddDatabaseSpecs(
        &specs, base::StrCat({"arc_", profile_key, "_web_data"}),
        arc_profile.path.AppendASCII("Web Data"),
        base::StrCat({manifest_profile_prefix, "Web Data"}),
        base::FilePath::FromUTF8Unsafe(
            base::StrCat({"Arc-", profile_key, "-Web-Data.sqlite"}))
            .value());
  }
  AddDatabaseSpecs(&specs, "ahoi_tab_tree",
                   ahoi_profile_path.AppendASCII(kTabTreeDatabaseFilename),
                   base::StrCat({"AhoiProfile/", kTabTreeDatabaseFilename}),
                   FILE_PATH_LITERAL("Ahoi-Tab-Tree.sqlite"),
                   /*primary_required=*/true);

  // Capture one complete source generation before copying. A later full pass
  // must match byte-for-byte and metadata-for-metadata, including optional
  // SQLite WAL/SHM presence. This permits unrelated inaccessible processes
  // without ever accepting a source that changed during the backup window.
  std::vector<BackupFileState> source_states(specs.size());
  bool source_changed = false;
  bool source_generation_captured = true;
  for (size_t index = 0; index < specs.size(); ++index) {
    if (!CaptureBackupFileState(specs[index], &source_states[index])) {
      source_generation_captured = false;
      break;
    }
    if (specs[index].is_arc_sidebar &&
        source_states[index].sha256 != expected_snapshot_token) {
      source_generation_captured = false;
      source_changed = true;
      break;
    }
  }
  if (!source_generation_captured) {
    result.status = source_changed ? ArcImportStatus::kSourceChanged
                                   : ArcImportStatus::kBackupError;
    return result;
  }

  std::vector<int64_t> present_file_sizes;
  present_file_sizes.reserve(source_states.size());
  for (const BackupFileState& state : source_states) {
    if (state.present) {
      present_file_sizes.push_back(state.size);
    }
  }
  constexpr ArcImportBackupLimits limits;
  static_assert(limits.max_retained_backups > 0);
  const std::optional<int64_t> free_disk_bytes =
      base::SysInfo::AmountOfFreeDiskSpace(ahoi_profile_path);
  uint64_t total_bytes = 0;
  if (!free_disk_bytes) {
    result.status = ArcImportStatus::kInsufficientDiskSpace;
    return result;
  }
  result.status = internal::CheckArcImportBackupResources(
      present_file_sizes, *free_disk_bytes, limits, &total_bytes);
  if (result.status != ArcImportStatus::kOk) {
    return result;
  }

  const ArcImportJournalReadResult journal =
      ReadArcImportJournal(ahoi_profile_path);
  if (journal.status != ArcImportStatus::kOk) {
    result.status = ArcImportStatus::kBackupError;
    return result;
  }
  std::set<std::string> protected_backups;
  if (journal.state == ArcImportJournalState::kPrepared &&
      (!journal.prepared || journal.prepared->backup_identifier.empty())) {
    result.status = ArcImportStatus::kBackupError;
    return result;
  }
  if (journal.state == ArcImportJournalState::kPrepared) {
    protected_backups.insert(journal.prepared->backup_identifier);
  }
  const size_t retained_before_creation = limits.max_retained_backups - 1;
  if (!internal::PruneArcImportBackupsForTesting(backup_root, protected_backups,
                                                 retained_before_creation) ||
      base::PathExists(backup_directory) ||
      !base::CreateDirectory(backup_directory) ||
      !base::SetPosixFilePermissions(backup_directory, 0700)) {
    result.status = ArcImportStatus::kBackupError;
    return result;
  }

  base::ListValue manifest_files;
  bool success = source_generation_captured;
  for (size_t index = 0; success && index < specs.size(); ++index) {
    if (!CopyAndVerify(specs[index], source_states[index], backup_directory,
                       &manifest_files, expected_snapshot_token)) {
      success = false;
      break;
    }
  }
  for (size_t index = 0; source_generation_captured && index < specs.size();
       ++index) {
    BackupFileState after;
    if (!CaptureBackupFileState(specs[index], &after) ||
        !BackupFileStatesMatch(source_states[index], after)) {
      source_changed = true;
      success = false;
      break;
    }
  }
  if (success && source_use_check.Run(arc_source)) {
    result.status = ArcImportStatus::kSourceInUse;
    success = false;
  }

  base::DictValue manifest;
  manifest.Set("version", kBackupManifestVersion);
  manifest.Set("backup_identifier", directory_name);
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
  if (success) {
    base::File manifest_file(temporary_manifest,
                             base::File::FLAG_OPEN | base::File::FLAG_WRITE |
                                 base::File::FLAG_NO_FOLLOW);
    success = manifest_file.IsValid() && manifest_file.Flush();
  }
  base::File::Error replace_error = base::File::FILE_OK;
  success =
      success &&
      base::ReplaceFile(temporary_manifest, manifest_path, &replace_error) &&
      base::SetPosixFilePermissions(manifest_path, 0600);
  success = success && FsyncDirectory(backup_directory) &&
            FsyncDirectory(backup_root) && FsyncDirectory(ahoi_directory) &&
            FsyncDirectory(ahoi_profile_path);
  if (!success) {
    base::DeleteFile(temporary_manifest);
    base::DeletePathRecursively(backup_directory);
    if (source_changed) {
      result.status = ArcImportStatus::kSourceChanged;
    }
    return result;
  }

  result.status = ArcImportStatus::kOk;
  result.backup_directory = backup_directory;
  result.backup_identifier = directory_name;
  result.manifest_sha256 = base::HexEncodeLower(crypto::hash::Sha256(json));
  return result;
}

}  // namespace

ArcImportBackupResult CreateArcImportBackup(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token) {
  return CreateArcImportBackupWithSourceUseCheck(
      ahoi_profile_path, arc_source, expected_snapshot_token,
      base::BindRepeating([](const ArcSource& source) {
        return IsArcApplicationRunning() || AreArcProfileFilesOpen(source);
      }));
}

namespace internal {

ArcImportBackupResult CreateArcImportBackupForTesting(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& arc_source,
    const std::string& expected_snapshot_token,
    ArcSourceUseCheck source_use_check) {
  return CreateArcImportBackupWithSourceUseCheck(
      ahoi_profile_path, arc_source, expected_snapshot_token, source_use_check);
}

}  // namespace internal

}  // namespace ahoi::importer::arc
