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

// ADR 0011 WS-ISO-10: separating an Arc profile targets another Profile, so
// it can never replay as the no-op of the all-in-one import.
TEST(ArcImportTransactionKeyTest, SeparatedArcProfilesArePartOfTheKey) {
  ArcImportTransactionSelection all_here{
      .selected_browser_profiles = {"Default", "Profile 1"}};
  ArcImportTransactionSelection separated = all_here;
  separated.separated_arc_profiles = {"Profile 1"};
  ArcImportTransactionSelection other_separated = all_here;
  other_separated.separated_arc_profiles = {"Default"};

  const std::string all_here_fingerprint =
      ComputeArcImportSelectionFingerprint(all_here);
  const std::string separated_fingerprint =
      ComputeArcImportSelectionFingerprint(separated);
  EXPECT_NE(all_here_fingerprint, separated_fingerprint);
  EXPECT_NE(separated_fingerprint,
            ComputeArcImportSelectionFingerprint(other_separated));
  EXPECT_NE(ComputeArcImportIdempotencyKey(kSnapshot, all_here_fingerprint),
            ComputeArcImportIdempotencyKey(kSnapshot, separated_fingerprint));
  // A browser profile name and a separated Arc profile never alias.
  ArcImportTransactionSelection moved{
      .selected_browser_profiles = {"Default"},
      .separated_arc_profiles = {"Profile 1"}};
  EXPECT_NE(ComputeArcImportSelectionFingerprint(moved),
            separated_fingerprint);
}

TEST(ArcImportTransactionKeyTest, SeparatedOrderingIsCanonical) {
  ArcImportTransactionSelection first{
      .selected_browser_profiles = {"Default"},
      .separated_arc_profiles = {"Profile 2", "Profile 1"}};
  ArcImportTransactionSelection second{
      .selected_browser_profiles = {"Default"},
      .separated_arc_profiles = {"Profile 1", "Profile 2"}};

  EXPECT_EQ(ComputeArcImportSelectionFingerprint(first),
            ComputeArcImportSelectionFingerprint(second));
}

// Journals written before WS-ISO-10 keep their key for the unchanged
// mapping: the canonical text only grows when something is separated.
TEST(ArcImportTransactionKeyTest, NoSeparationKeepsTheLegacyKey) {
  ArcImportTransactionSelection selection{
      .selected_browser_profiles = {"Default"}};
  // sha256("arc-selection-v1\nsidebar=1\nsplits=1\nconflict=0\n"
  //        "profile_sha256=" + sha256("Default")), the pre-WS-ISO-10 text.
  EXPECT_EQ("8ff79cc02c63f941536d41b802375a165a014785008e019a2365ce774760cca6",
            ComputeArcImportSelectionFingerprint(selection));
}

}  // namespace

}  // namespace ahoi::importer::arc
