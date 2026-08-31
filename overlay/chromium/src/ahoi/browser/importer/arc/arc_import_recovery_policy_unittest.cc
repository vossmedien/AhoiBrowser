// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_recovery_policy.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kPrevious[] =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr char kExpected[] =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr char kForeign[] =
    "3333333333333333333333333333333333333333333333333333333333333333";

ArcImportPreparedState Prepared(ArcImportPreparedPhase phase) {
  return {.previous_tree_sha256 = kPrevious,
          .expected_tree_sha256 = kExpected,
          .phase = phase};
}

TEST(ArcImportRecoveryPolicyTest,
     ServiceStateMachineRestoresJournalWithoutTreeWriteWhenAlreadyPrevious) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kTreeOnly), kPrevious);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kRestorePreviousJournal,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     ServiceStateMachineAuthorizesExactlyOneRollbackForExpectedTree) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kTreeOnly), kExpected);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kApplyPreviousTree,
            decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kRestorePreviousJournal,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     ServiceStateMachineNeverWritesForeignTreeAndRequiresManualRecovery) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kTreeOnly), kForeign);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     EqualPreviousAndExpectedPrefersJournalOnlyWithoutTreeWrite) {
  ArcImportPreparedState prepared = Prepared(ArcImportPreparedPhase::kTreeOnly);
  prepared.expected_tree_sha256 = kPrevious;

  const ArcImportRecoveryDecision decision =
      DecideArcImportPreparedRecovery(prepared, kPrevious);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kRestorePreviousJournal,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     RuntimeUncertaintyKeepsManualAfterAlreadyPreviousTree) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kRuntimeMayHaveStarted), kPrevious);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     RuntimeUncertaintyNeverMutatesExpectedTreeAndKeepsManual) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kRuntimeMayHaveStarted), kExpected);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest,
     RuntimePersistedReceiptStillNeverAuthorizesOneSidedTreeRecovery) {
  const ArcImportRecoveryDecision decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kRuntimePersisted), kExpected);

  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);
}

TEST(ArcImportRecoveryPolicyTest, ManualAndMalformedStateNeverAuthorizeWrite) {
  ArcImportPreparedState manual =
      Prepared(ArcImportPreparedPhase::kManualRecoveryRequired);
  ArcImportRecoveryDecision decision =
      DecideArcImportPreparedRecovery(manual, kExpected);
  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);

  ArcImportPreparedState malformed =
      Prepared(ArcImportPreparedPhase::kTreeOnly);
  malformed.previous_tree_sha256 = "invalid";
  decision = DecideArcImportPreparedRecovery(malformed, kExpected);
  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);

  decision = DecideArcImportPreparedRecovery(
      Prepared(ArcImportPreparedPhase::kTreeOnly), "invalid");
  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);

  ArcImportPreparedState unknown_phase =
      Prepared(static_cast<ArcImportPreparedPhase>(99));
  decision = DecideArcImportPreparedRecovery(unknown_phase, kExpected);
  EXPECT_EQ(ArcImportRecoveryTreeAction::kNone, decision.tree_action);
  EXPECT_EQ(ArcImportRecoveryCompletion::kManualRecoveryRequired,
            decision.completion);
}

}  // namespace

}  // namespace ahoi::importer::arc
