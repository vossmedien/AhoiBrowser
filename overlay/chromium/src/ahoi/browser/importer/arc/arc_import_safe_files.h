// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SAFE_FILES_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SAFE_FILES_H_

#include <cstdint>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"

// No-follow, single-link file primitives the Arc import backup uses to copy
// and verify the Arc profile and its own owner-only manifest (split from
// arc_import_backup.cc, source line budget).
namespace ahoi::importer::arc::safe_files {

// Identity of a regular file: content hash plus size and timestamps, so a
// copy can prove the source did not change underneath it.
struct BackupFileState {
  bool present = false;
  std::string sha256;
  int64_t size = 0;
  int64_t modified_unix_ms = 0;
  int64_t created_unix_ms = 0;
};

bool BackupFileStatesMatch(const BackupFileState& left,
                           const BackupFileState& right);

base::File OpenSafeRegularFile(const base::FilePath& path);

bool HashOpenedRegularFile(base::File* file, BackupFileState* state);

bool HashRegularFile(const base::FilePath& path, BackupFileState* state);

bool IsOwnerOnlyOpenedRegularFile(base::File* file);

bool ReadStableOwnerOnlyFile(base::File* file,
                             int64_t max_bytes,
                             std::string* contents);

bool IsPathMissingNoFollow(const base::FilePath& path);

bool FsyncDirectory(const base::FilePath& path);

bool IsOwnerOnlyDirectory(const base::FilePath& path);

}  // namespace ahoi::importer::arc::safe_files

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SAFE_FILES_H_
