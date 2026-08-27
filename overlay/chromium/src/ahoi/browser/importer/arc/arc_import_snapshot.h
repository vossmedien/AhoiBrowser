// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SNAPSHOT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SNAPSHOT_H_

#include "ahoi/browser/importer/arc/arc_import_types.h"

namespace ahoi::importer::arc {

// Captures a bounded, stable, read-only in-memory snapshot. The source path is
// checked before and after the read, and a concurrent size/mtime change aborts
// the operation instead of parsing a partial generation.
ArcSnapshotResult CaptureArcSnapshot(const ArcSource& source);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_SNAPSHOT_H_
