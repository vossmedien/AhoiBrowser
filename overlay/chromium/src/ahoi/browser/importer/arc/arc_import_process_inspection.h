// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROCESS_INSPECTION_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROCESS_INSPECTION_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_discovery.h"

// libproc process enumeration and open-file inspection that the Arc source
// mutability gate uses to prove Arc no longer holds its profile files (split
// from arc_import_discovery.cc, source line budget).
namespace ahoi::importer::arc::process_inspection {

struct ProcessIdentity {
  pid_t pid;
  uint64_t start_time_seconds;
  uint64_t start_time_microseconds;
};

struct ProcessMetadata {
  ProcessIdentity identity;
  internal::ProcessOwnership ownership;
  uint32_t open_file_count;
};

struct ProcessObservation {
  std::optional<ProcessMetadata> metadata;
  internal::ProcessLiveness liveness;
};

enum class OpenFileInspectionResult {
  kClear,
  kRelevantFileOpen,
  kFailed,
};

// Every live PID, sorted and unique, or nullopt when enumeration is not
// trustworthy (the current process must be among them).
std::optional<std::vector<pid_t>> ListAllPids();

// Metadata bound to PID and start time, plus the best liveness evidence.
ProcessObservation ObserveProcess(pid_t pid);

// Whether `pid` holds a file relevant to `source`, retrying descriptor
// snapshots while the process identity stays the same.
OpenFileInspectionResult InspectOpenFilesWithRetry(pid_t pid,
                                                   ProcessMetadata metadata,
                                                   const ArcSource& source);

}  // namespace ahoi::importer::arc::process_inspection

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_PROCESS_INSPECTION_H_
