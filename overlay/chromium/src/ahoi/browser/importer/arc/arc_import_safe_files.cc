// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_safe_files.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc::safe_files {

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

bool IsOwnerOnlyDirectory(const base::FilePath& path) {
  struct stat state;
  return lstat(path.value().c_str(), &state) == 0 && S_ISDIR(state.st_mode) &&
         !S_ISLNK(state.st_mode) && state.st_uid == getuid() &&
         (state.st_mode & 0077) == 0;
}

}  // namespace ahoi::importer::arc::safe_files
