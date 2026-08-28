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
// closed. Processes that exited during inspection and foreign-user processes
// do not block solely because their executable path is inaccessible.
bool IsArcApplicationRunning();

// Enumerates open vnode handles read-only and blocks while any process has the
// sidebar file or a relevant file in one of this source's selected profiles
// open. Relevant profile files include Preferences, Bookmarks, and the
// History, Favicons, and Web Data databases with their WAL/SHM sidecars. PID
// enumeration failures and executable, identity, or file-descriptor inspection
// failures for a still-live current-user process fail closed. Processes that
// exited during inspection and foreign-user processes do not block solely
// because their descriptors are inaccessible.
bool AreArcProfileFilesOpen(const ArcSource& source);

namespace internal {

enum class ProcessOwnership {
  kCurrentUser,
  kForeignUser,
  kUnknown,
};

enum class ProcessLiveness {
  kAliveAndSignalable,
  kAliveButNotSignalable,
  kExited,
  kUnknown,
};

// Pure path predicates shared by production discovery and focused unit tests.
bool IsArcBundleExecutablePath(const base::FilePath& executable);
bool IsRelevantArcSourcePath(const ArcSource& source,
                             const base::FilePath& open_path);

// Pure fail-closed policy used after executable, identity, or descriptor
// inspection fails. A known current-user process blocks unless it exited. An
// inaccessible foreign-user process is ignored. Unknown ownership blocks when
// the process is signalable or its liveness cannot be established, but not when
// it is known to be inaccessible to this user.
bool ShouldBlockOnProcessInspectionFailure(ProcessOwnership ownership,
                                           ProcessLiveness liveness);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
