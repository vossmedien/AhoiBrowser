// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ZEN_ZEN_APPLICATION_DISCOVERY_H_
#define AHOI_BROWSER_IMPORTER_ZEN_ZEN_APPLICATION_DISCOVERY_H_

#include <vector>

#include "base/files/file_path.h"

namespace ahoi::importer::zen {

struct ZenApplicationState {
  base::FilePath bundle_path;
  bool installed = false;
  bool running = false;
};

enum class ZenImportAvailability {
  kNotInstalled,
  kSourceRunning,
  kAvailable,
};

// Converts authenticated bundle/process discovery into the user-facing import
// capability. Callers can distinguish an absent source from one that must be
// closed before Chromium's normal Firefox-compatible importer may read it.
ZenImportAvailability GetZenImportAvailability(
    const ZenApplicationState& state);

// Detects a normal release or Twilight bundle in the system/user Applications
// folders and independently checks live executable paths. Stale Firefox lock
// files are deliberately not treated as process evidence.
ZenApplicationState InspectDefaultZenApplication();

namespace internal {

// Pure predicates and injected discovery used by focused false-positive tests.
bool IsZenBundleExecutablePath(const base::FilePath& executable);
bool IsSafeZenApplicationBundle(const base::FilePath& bundle_path);
ZenApplicationState InspectZenApplicationAt(
    const std::vector<base::FilePath>& application_roots,
    const std::vector<base::FilePath>& running_executables);

}  // namespace internal

}  // namespace ahoi::importer::zen

#endif  // AHOI_BROWSER_IMPORTER_ZEN_ZEN_APPLICATION_DISCOVERY_H_
