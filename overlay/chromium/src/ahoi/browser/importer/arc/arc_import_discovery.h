// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_

#include <vector>

#include "ahoi/browser/importer/arc/arc_import_types.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace ahoi::importer::arc {

struct ArcApplicationState {
  base::FilePath bundle_path;
  bool installed = false;
  bool running = false;
};

enum class ArcImportAvailability {
  kNotInstalled,
  kNoSafeProfiles,
  kSourceRunning,
  kAvailable,
};

// Authenticates Arc.app in the normal system/user Applications roots by bundle
// identifier and executable, then binds running main/helper processes to that
// exact bundle. A stale Application Support directory is never installation
// evidence.
ArcApplicationState InspectDefaultArcApplication();
ArcImportAvailability GetDefaultArcImportAvailability();

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
// discovery is argv-independent and an enumeration failure fails closed. An
// unreadable executable path alone is not treated as Arc evidence because
// sandboxed, unrelated macOS helpers commonly have that state; the separate
// source-handle inspection below remains mandatory before publishing a usable
// preview and is repeated independently before commit and backup.
bool IsArcApplicationRunning();

// Enumerates open vnode handles read-only and blocks while any process has the
// sidebar file or a relevant file in one of this source's selected profiles
// open. Relevant profile files include Preferences, Bookmarks, and the
// History, Favicons, and Web Data databases with their WAL/SHM sidecars. PID
// enumeration failure fails closed. Every readable process is inspected
// regardless of whether its executable path was available and regardless of
// ownership, so a relevant handle held by a foreign-user process also blocks.
// Descriptor races are retried from a fresh snapshot while PID and process
// start time remain stable. ESRCH or a verified PID/start-time replacement
// ends inspection of the originally enumerated process; PROC_FLAG_INEXIT does
// not. A repeatedly inaccessible or high-churn unrelated process does not
// block solely because its descriptors cannot be inspected: sidebar snapshot
// metadata and hashes plus verified backup copies provide the mutation gate.
bool AreArcProfileFilesOpen(const ArcSource& source);

namespace internal {

using ArcBundleAuthenticationCallback =
    base::RepeatingCallback<bool(const base::FilePath&)>;

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
  kInconclusive,
};

enum class OpenFileInspectionEvidence {
  kNoRelevantSourceHandle,
  kRelevantSourceHandle,
  kInspectionInconclusive,
};

// Pure path predicates shared by production discovery and focused unit tests.
bool IsArcBundleExecutablePath(const base::FilePath& executable);
bool IsSafeArcApplicationBundle(const base::FilePath& bundle_path);
ArcApplicationState InspectArcApplicationAt(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables);
// Test fixtures are intentionally unsigned. Tests may inject only the
// code-signature decision; bundle structure and Info.plist identity are still
// validated by production code, and this seam is never used for real roots.
ArcApplicationState InspectArcApplicationAtForTesting(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables,
    ArcBundleAuthenticationCallback bundle_authenticator);
bool IsRelevantArcSourcePath(const ArcSource& source,
                             const base::FilePath& open_path);

// Pure classification policy for a non-retryable process-inspection failure.
// kExited is produced only by ESRCH. kInExitWithoutExitEvidence remains live.
// Current-user and unknown live failures stay inconclusive; foreign-user
// access failures are ignored. Inconclusive generic processes do not by
// themselves block import: positive Arc identity/handle evidence blocks, and
// the immutable before/copy/after generation fingerprint catches mutation.
bool IsProcessInspectionFailureInconclusive(ProcessOwnership ownership,
                                            ProcessLiveness liveness);

// Pure retry policy for an incomplete descriptor snapshot. A verified PID and
// start-time change ends inspection of the originally enumerated process. A
// stable current-user process becomes inconclusive after at least three
// failures for the same identity. If identity cannot be refreshed, a
// still-live process whose last known ownership is the current user receives
// the same classification when retries are exhausted. Foreign-user failures
// receive the same retries, but become ignorable when descriptor inspection
// remains inaccessible.
ProcessInspectionFailureDisposition DecideOpenFileInspectionFailure(
    ProcessOwnership ownership,
    ProcessLiveness liveness,
    ProcessIdentityMatch identity_match,
    int consecutive_same_process_failures,
    bool can_retry);

// Positive readable-handle evidence blocks independently of process ownership.
// Inconclusive generic-process inspection does not masquerade as positive Arc
// evidence; source immutability remains enforced by the backup generation
// fingerprint.
bool ShouldBlockOnOpenFileInspectionEvidence(
    OpenFileInspectionEvidence evidence,
    ProcessOwnership ownership);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
