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

// Returns true only for Arc's signed-app main executable shape
// (.../Arc.app/Contents/MacOS/Arc). Helpers are deliberately ignored. Import
// must not snapshot while this process is present because Arc can replace its
// sidebar file atomically at any time.
bool IsArcApplicationRunning();

// Enumerates open vnode handles read-only and blocks while any process has a
// file below this Arc source's User Data directory open. This catches detached
// helpers and external SQLite readers even after the main process exits.
bool AreArcProfileFilesOpen(const ArcSource& source);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_DISCOVERY_H_
