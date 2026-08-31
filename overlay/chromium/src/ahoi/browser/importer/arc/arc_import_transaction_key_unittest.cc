// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_transaction_key.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kSnapshot[] =
    "1111111111111111111111111111111111111111111111111111111111111111";

TEST(ArcImportTransactionKeyTest, SplitUpgradeIsNotAFalseNoOp) {
  ArcImportTransactionSelection without_splits{
      .reconstruct_splits = false, .selected_browser_profiles = {"Default"}};
  ArcImportTransactionSelection with_splits = without_splits;
  with_splits.reconstruct_splits = true;

  const std::string first_fingerprint =
      ComputeArcImportSelectionFingerprint(without_splits);
  const std::string second_fingerprint =
      ComputeArcImportSelectionFingerprint(with_splits);

  EXPECT_NE(first_fingerprint, second_fingerprint);
  EXPECT_NE(ComputeArcImportIdempotencyKey(kSnapshot, first_fingerprint),
            ComputeArcImportIdempotencyKey(kSnapshot, second_fingerprint));
}

TEST(ArcImportTransactionKeyTest, IncludesConflictAndSelectedComponents) {
  ArcImportTransactionSelection rename{
      .conflict_resolution = ArcConflictResolution::kRename,
      .selected_browser_profiles = {"Default", "Profile 2"}};
  ArcImportTransactionSelection merge = rename;
  merge.conflict_resolution = ArcConflictResolution::kMerge;
  ArcImportTransactionSelection fewer_profiles = rename;
  fewer_profiles.selected_browser_profiles = {"Default"};

  EXPECT_NE(ComputeArcImportSelectionFingerprint(rename),
            ComputeArcImportSelectionFingerprint(merge));
  EXPECT_NE(ComputeArcImportSelectionFingerprint(rename),
            ComputeArcImportSelectionFingerprint(fewer_profiles));
}

TEST(ArcImportTransactionKeyTest, ProfileOrderingIsCanonical) {
  ArcImportTransactionSelection first{
      .selected_browser_profiles = {"Profile 2", "Default"}};
  ArcImportTransactionSelection second{
      .selected_browser_profiles = {"Default", "Profile 2"}};

  EXPECT_EQ(ComputeArcImportSelectionFingerprint(first),
            ComputeArcImportSelectionFingerprint(second));
}

}  // namespace

}  // namespace ahoi::importer::arc
