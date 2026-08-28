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
// present. Import must not snapshot while either Arc's main process or a
// detached helper is present because both can mutate sidebar or profile data.
bool IsArcApplicationRunning();

// Enumerates open vnode handles read-only and blocks while any process has the
// sidebar file or a relevant file in one of this source's selected profiles
// open. Relevant profile files include Preferences, Bookmarks, and the
// History, Favicons, and Web Data databases with their WAL/SHM sidecars.
bool AreArcProfileFilesOpen(const ArcSource& source);

namespace internal {

// Pure path predicates shared by production discovery and focused unit tests.
bool IsArcBundleExecutablePath(const base::FilePath& executable);
bool IsRelevantArcSourcePath(const ArcSource& source,
                             const base::FilePath& open_path);

}  // namespace internal

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
