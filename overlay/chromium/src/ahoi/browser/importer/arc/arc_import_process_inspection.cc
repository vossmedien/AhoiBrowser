// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_process_inspection.h"

#include <libproc.h>
#include <signal.h>
#include <sys/proc_info.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_path.h"

namespace ahoi::importer::arc {

namespace {

constexpr size_t kPidEnumerationHeadroom = 64;
constexpr size_t kFileDescriptorHeadroom = 16;
constexpr int kMaxPidEnumerationAttempts = 4;
constexpr int kMaxOpenFileInspectionAttempts = 3;

}  // namespace

namespace internal {

bool IsProcessInspectionFailureInconclusive(ProcessOwnership ownership,
                                            ProcessLiveness liveness) {
  if (liveness == ProcessLiveness::kExited ||
      ownership == ProcessOwnership::kForeignUser) {
    return false;
  }
  if (ownership == ProcessOwnership::kCurrentUser) {
    return true;
  }
  return liveness != ProcessLiveness::kAliveButNotSignalable;
}

ProcessInspectionFailureDisposition DecideOpenFileInspectionFailure(
    ProcessOwnership ownership,
    ProcessLiveness liveness,
    ProcessIdentityMatch identity_match,
    int consecutive_same_process_failures,
    bool can_retry) {
  if (liveness == ProcessLiveness::kExited ||
      identity_match == ProcessIdentityMatch::kDifferentProcess) {
    return ProcessInspectionFailureDisposition::kIgnore;
  }
  if (identity_match == ProcessIdentityMatch::kSameProcess &&
      consecutive_same_process_failures >= kMaxOpenFileInspectionAttempts) {
    return IsProcessInspectionFailureInconclusive(ownership, liveness)
               ? ProcessInspectionFailureDisposition::kInconclusive
               : ProcessInspectionFailureDisposition::kIgnore;
  }
  if (can_retry) {
    return ProcessInspectionFailureDisposition::kRetry;
  }
  return IsProcessInspectionFailureInconclusive(ownership, liveness)
             ? ProcessInspectionFailureDisposition::kInconclusive
             : ProcessInspectionFailureDisposition::kIgnore;
}

bool ShouldBlockOnOpenFileInspectionEvidence(
    OpenFileInspectionEvidence evidence,
    ProcessOwnership ownership) {
  // Ownership affects inaccessible inspection, never positive evidence.
  static_cast<void>(ownership);
  return evidence == OpenFileInspectionEvidence::kRelevantSourceHandle;
}

}  // namespace internal

namespace process_inspection {

namespace {

std::optional<ProcessMetadata> ReadProcessMetadata(pid_t pid) {
  proc_bsdinfo process_info{};
  const int received_bytes =
      proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &process_info,
                   static_cast<int>(sizeof(process_info)));
  if (received_bytes != static_cast<int>(sizeof(process_info)) || pid <= 0 ||
      process_info.pbi_pid != static_cast<uint32_t>(pid)) {
    return std::nullopt;
  }

  const bool is_current_user =
      process_info.pbi_uid == geteuid() || process_info.pbi_ruid == getuid();
  return ProcessMetadata{
      .identity =
          ProcessIdentity{
              .pid = static_cast<pid_t>(process_info.pbi_pid),
              .start_time_seconds = process_info.pbi_start_tvsec,
              .start_time_microseconds = process_info.pbi_start_tvusec,
          },
      .ownership = is_current_user ? internal::ProcessOwnership::kCurrentUser
                                   : internal::ProcessOwnership::kForeignUser,
      .open_file_count = process_info.pbi_nfiles,
  };
}

internal::ProcessLiveness ProbeProcessLiveness(pid_t pid) {
  errno = 0;
  if (kill(pid, 0) == 0) {
    return internal::ProcessLiveness::kAlive;
  }
  if (errno == ESRCH) {
    return internal::ProcessLiveness::kExited;
  }
  if (errno == EPERM) {
    return internal::ProcessLiveness::kAliveButNotSignalable;
  }
  return internal::ProcessLiveness::kUnknown;
}

internal::ProcessIdentityMatch CompareProcessIdentity(
    const ProcessIdentity& expected,
    const std::optional<ProcessMetadata>& actual) {
  if (!actual) {
    return internal::ProcessIdentityMatch::kUnknown;
  }
  if (expected.pid == actual->identity.pid &&
      expected.start_time_seconds == actual->identity.start_time_seconds &&
      expected.start_time_microseconds ==
          actual->identity.start_time_microseconds) {
    return internal::ProcessIdentityMatch::kSameProcess;
  }
  return internal::ProcessIdentityMatch::kDifferentProcess;
}

OpenFileInspectionResult InspectOpenFilesOnce(pid_t pid,
                                              uint32_t expected_open_file_count,
                                              const ArcSource& source) {
  if (expected_open_file_count == 0) {
    return OpenFileInspectionResult::kClear;
  }

  const int required_bytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
  if (required_bytes <= 0 ||
      required_bytes % static_cast<int>(sizeof(proc_fdinfo)) != 0) {
    return OpenFileInspectionResult::kFailed;
  }

  size_t capacity = static_cast<size_t>(required_bytes) / sizeof(proc_fdinfo);
  if (capacity > std::numeric_limits<size_t>::max() - kFileDescriptorHeadroom) {
    return OpenFileInspectionResult::kFailed;
  }
  capacity += kFileDescriptorHeadroom;
  constexpr size_t kMaxFileDescriptorCapacity =
      static_cast<size_t>(std::numeric_limits<int>::max()) /
      sizeof(proc_fdinfo);
  if (capacity == 0 || capacity > kMaxFileDescriptorCapacity) {
    return OpenFileInspectionResult::kFailed;
  }

  std::vector<proc_fdinfo> descriptors(capacity);
  const int buffer_bytes = static_cast<int>(capacity * sizeof(proc_fdinfo));
  const int received_bytes =
      proc_pidinfo(pid, PROC_PIDLISTFDS, 0, descriptors.data(), buffer_bytes);
  if (received_bytes <= 0 || received_bytes > buffer_bytes ||
      received_bytes % static_cast<int>(sizeof(proc_fdinfo)) != 0 ||
      received_bytes == buffer_bytes) {
    return OpenFileInspectionResult::kFailed;
  }
  descriptors.resize(static_cast<size_t>(received_bytes) / sizeof(proc_fdinfo));

  bool had_descriptor_failure = false;
  for (const proc_fdinfo& descriptor : descriptors) {
    if (descriptor.proc_fdtype != PROX_FDTYPE_VNODE) {
      continue;
    }
    vnode_fdinfowithpath vnode_info{};
    const int vnode_bytes =
        proc_pidfdinfo(pid, descriptor.proc_fd, PROC_PIDFDVNODEPATHINFO,
                       &vnode_info, sizeof(vnode_info));
    if (vnode_bytes != static_cast<int>(sizeof(vnode_info))) {
      had_descriptor_failure = true;
      continue;
    }
    if (internal::IsRelevantArcSourcePath(
            source, base::FilePath(vnode_info.pvip.vip_path))) {
      return OpenFileInspectionResult::kRelevantFileOpen;
    }
  }
  return had_descriptor_failure ? OpenFileInspectionResult::kFailed
                                : OpenFileInspectionResult::kClear;
}

}  // namespace

std::optional<std::vector<pid_t>> ListAllPids() {
  const int estimated_count = proc_listallpids(nullptr, 0);
  if (estimated_count <= 0) {
    return std::nullopt;
  }

  size_t capacity = static_cast<size_t>(estimated_count);
  if (capacity > std::numeric_limits<size_t>::max() - kPidEnumerationHeadroom) {
    return std::nullopt;
  }
  capacity += kPidEnumerationHeadroom;

  for (int attempt = 0; attempt < kMaxPidEnumerationAttempts; ++attempt) {
    constexpr size_t kMaxPidCapacity =
        static_cast<size_t>(std::numeric_limits<int>::max()) / sizeof(pid_t);
    if (capacity == 0 || capacity > kMaxPidCapacity) {
      return std::nullopt;
    }

    std::vector<pid_t> pids(capacity);
    const int count = proc_listallpids(
        pids.data(), static_cast<int>(capacity * sizeof(pid_t)));
    if (count <= 0 || static_cast<size_t>(count) > capacity) {
      return std::nullopt;
    }
    if (static_cast<size_t>(count) == capacity) {
      if (capacity > kMaxPidCapacity / 2) {
        return std::nullopt;
      }
      capacity *= 2;
      continue;
    }

    pids.resize(static_cast<size_t>(count));
    std::erase_if(pids, [](pid_t pid) { return pid <= 0; });
    std::ranges::sort(pids);
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    if (pids.empty() ||
        !std::binary_search(pids.begin(), pids.end(), getpid())) {
      return std::nullopt;
    }
    return pids;
  }
  return std::nullopt;
}

ProcessObservation ObserveProcess(pid_t pid) {
  std::optional<ProcessMetadata> metadata = ReadProcessMetadata(pid);
  if (metadata) {
    // The metadata and identity came from one proc_bsdinfo snapshot, so no
    // separate liveness probe can accidentally describe a reused PID.
    return {.metadata = std::move(metadata),
            .liveness = internal::ProcessLiveness::kAlive};
  }

  const internal::ProcessLiveness liveness = ProbeProcessLiveness(pid);
  if (liveness == internal::ProcessLiveness::kExited) {
    return {.metadata = std::nullopt, .liveness = liveness};
  }

  // A process may become inspectable between the metadata and signal probes.
  // Re-read metadata so any usable liveness evidence is bound to PID+start
  // time.
  metadata = ReadProcessMetadata(pid);
  if (metadata) {
    return {.metadata = std::move(metadata),
            .liveness = internal::ProcessLiveness::kAlive};
  }
  return {.metadata = std::nullopt, .liveness = liveness};
}

OpenFileInspectionResult InspectOpenFilesWithRetry(pid_t pid,
                                                   ProcessMetadata metadata,
                                                   const ArcSource& source) {
  int consecutive_same_process_failures = 0;
  for (int attempt = 0; attempt < kMaxOpenFileInspectionAttempts; ++attempt) {
    // A descriptor can disappear between PROC_PIDLISTFDS and proc_pidfdinfo.
    // Retry the whole snapshot, and only accept a clear result while the PID,
    // start time, and descriptor count still match the pre-snapshot metadata.
    const OpenFileInspectionResult result =
        InspectOpenFilesOnce(pid, metadata.open_file_count, source);
    if (result == OpenFileInspectionResult::kRelevantFileOpen) {
      return result;
    }

    const ProcessObservation observation = ObserveProcess(pid);
    const internal::ProcessIdentityMatch identity_match =
        CompareProcessIdentity(metadata.identity, observation.metadata);
    if (identity_match == internal::ProcessIdentityMatch::kDifferentProcess) {
      // This PID now names a process created after ListAllPids(). Do not carry
      // the original process's failure history into its replacement.
      return OpenFileInspectionResult::kClear;
    }

    const bool descriptor_snapshot_stable =
        observation.metadata &&
        identity_match == internal::ProcessIdentityMatch::kSameProcess &&
        observation.metadata->open_file_count == metadata.open_file_count;
    if (result == OpenFileInspectionResult::kClear &&
        descriptor_snapshot_stable) {
      return result;
    }

    if (identity_match == internal::ProcessIdentityMatch::kSameProcess) {
      ++consecutive_same_process_failures;
    } else {
      consecutive_same_process_failures = 0;
    }
    const bool can_retry = attempt + 1 < kMaxOpenFileInspectionAttempts;
    const internal::ProcessInspectionFailureDisposition disposition =
        internal::DecideOpenFileInspectionFailure(
            observation.metadata ? observation.metadata->ownership
                                 : metadata.ownership,
            observation.liveness, identity_match,
            consecutive_same_process_failures, can_retry);
    switch (disposition) {
      case internal::ProcessInspectionFailureDisposition::kRetry:
        if (observation.metadata) {
          metadata = *observation.metadata;
        }
        break;
      case internal::ProcessInspectionFailureDisposition::kIgnore:
        return OpenFileInspectionResult::kClear;
      case internal::ProcessInspectionFailureDisposition::kInconclusive:
        return OpenFileInspectionResult::kFailed;
    }
  }
  return OpenFileInspectionResult::kFailed;
}

}  // namespace process_inspection

}  // namespace ahoi::importer::arc
