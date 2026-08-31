// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TREE_FINGERPRINT_H_
#define AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TREE_FINGERPRINT_H_

#include <string>
#include <string_view>

#include "ahoi/browser/tab_tree/tab_tree_model.h"

namespace ahoi::importer::arc {

// Returns a domain-separated SHA-256 over every persisted TabTree field. Input
// collection order is canonicalized without ever serializing private titles or
// URLs into the import journal. Callers must run this work off the UI sequence.
std::string ComputeArcImportTreeFingerprint(
    const tab_tree::TabTreeSnapshot& snapshot);

bool IsArcImportTreeFingerprint(std::string_view value);

}  // namespace ahoi::importer::arc

#endif  // AHOI_BROWSER_IMPORTER_ARC_ARC_IMPORT_TREE_FINGERPRINT_H_
