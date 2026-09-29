// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction_key.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"

namespace ahoi::importer::arc {

namespace {

std::string Sha256(std::string_view value) {
  return base::HexEncodeLower(crypto::hash::Sha256(value));
}

}  // namespace

std::string ComputeArcImportSelectionFingerprint(
    const ArcImportTransactionSelection& selection) {
  std::vector<std::string> profiles = selection.selected_browser_profiles;
  std::ranges::sort(profiles);
  std::string canonical = "arc-selection-v1\nsidebar=";
  canonical += selection.import_sidebar ? "1" : "0";
  canonical += "\nsplits=";
  canonical += selection.reconstruct_splits ? "1" : "0";
  canonical += "\nconflict=";
  canonical +=
      base::NumberToString(static_cast<int>(selection.conflict_resolution));
  for (const std::string& profile : profiles) {
    canonical += "\nprofile_sha256=";
    canonical += Sha256(profile);
  }
  return Sha256(canonical);
}

std::string ComputeArcImportIdempotencyKey(
    const std::string& snapshot_hash,
    const std::string& selection_fingerprint) {
  return Sha256("arc-import-v1\n" + snapshot_hash + "\n" +
                selection_fingerprint);
}

}  // namespace ahoi::importer::arc
