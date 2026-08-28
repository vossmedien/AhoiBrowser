// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"

namespace ahoi::importer::arc {

// Discovers Arc below a trusted macOS Application Support directory. The
// function reads metadata only and rejects traversal and symlinks at every
// importer-owned path component.
ArcDiscoveryResult DiscoverArcSourceAt(
    const base::FilePath& application_support_dir);

// Resolves the current user's macOS Application Support directory and delegates
// to DiscoverArcSourceAt().
ArcDiscoveryResult DiscoverDefaultArcSource();

// Returns true while any process whose executable is inside Arc.app is
// present; every positively identified main or helper process blocks. PID
// discovery is argv-independent. Enumeration failures and failures that
// prevent a still-live current-user process from being ruled out as Arc fail
// closed. PROC_FLAG_INEXIT is not proof that a process ended; only an ESRCH
// liveness probe proves termination. Foreign-user processes do not block solely
// because their executable path is inaccessible.
bool IsArcApplicationRunning();

// Enumerates open vnode handles read-only and blocks while any process has the
// sidebar file or a relevant file in one of this source's selected profiles
// open. Relevant profile files include Preferences, Bookmarks, and the
// History, Favicons, and Web Data databases with their WAL/SHM sidecars. PID
// enumeration failures and executable, identity, or file-descriptor inspection
// failures for a still-live current-user process fail closed. Every readable
// process is inspected regardless of ownership, so a relevant handle held by a
// foreign-user process also blocks. Descriptor races are retried from a fresh
// snapshot while PID and process start time remain stable. ESRCH or a verified
// PID/start-time replacement ends inspection of the originally enumerated
// process; PROC_FLAG_INEXIT does not. A repeatedly inaccessible foreign-user
// process does not block solely because its descriptors cannot be inspected.
bool AreArcProfileFilesOpen(const ArcSource& source);

namespace internal {

enum class ProcessOwnership {
  kCurrentUser,
  kForeignUser,
  kUnknown,
};

enum class ProcessLiveness {
  kAlive,
  kAliveButNotSignalable,
  // PROC_FLAG_INEXIT without an ESRCH probe is not proof of termination. This
  // state exists for the pure policy and its regression test; production
  // metadata deliberately does not branch on PROC_FLAG_INEXIT.
  kInExitWithoutExitEvidence,
  kExited,
  kUnknown,
};

enum class ProcessIdentityMatch {
  kSameProcess,
  kDifferentProcess,
  kUnknown,
};

enum class ProcessInspectionFailureDisposition {
  kRetry,
  kIgnore,
  kBlock,
};

enum class OpenFileInspectionEvidence {
  kNoRelevantSourceHandle,
  kRelevantSourceHandle,
};

// Pure path predicates shared by production discovery and focused unit tests.
bool IsArcBundleExecutablePath(const base::FilePath& executable);
bool IsRelevantArcSourcePath(const ArcSource& source,
                             const base::FilePath& open_path);

// Pure fail-closed policy for a non-retryable process-inspection failure.
// kExited is produced only by ESRCH. kInExitWithoutExitEvidence remains live.
// Foreign-user access failures do not block; unknown ownership blocks unless
// EPERM establishes that the process is inaccessible to this user.
bool ShouldBlockOnProcessInspectionFailure(ProcessOwnership ownership,
                                           ProcessLiveness liveness);

// Pure retry policy for an incomplete descriptor snapshot. A verified PID and
// start-time change ends inspection of the originally enumerated process. A
// stable current-user process only blocks after at least three failures for the
// same identity. Foreign-user failures receive the same retries, but never
// block solely because descriptor inspection remained inaccessible.
ProcessInspectionFailureDisposition DecideOpenFileInspectionFailure(
    ProcessOwnership ownership,
    ProcessLiveness liveness,
    ProcessIdentityMatch identity_match,
    int consecutive_same_process_failures,
    bool can_retry);

// Positive readable-handle evidence blocks independently of process ownership.
// Failed or incomplete inspection is handled by the retry policy above.
bool ShouldBlockOnOpenFileInspectionEvidence(
    OpenFileInspectionEvidence evidence,
    ProcessOwnership ownership);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
