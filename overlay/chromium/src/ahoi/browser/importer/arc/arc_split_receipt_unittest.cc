// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_split_receipt.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/token.h"
#include "base/uuid.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"
#include "components/split_tabs/split_tab_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

namespace {

base::Uuid Uuid(const char* value) {
  return base::Uuid::ParseLowercase(value);
}

constexpr SessionID SessionId(int value) {
  return SessionID::FromSerializedValue(value);
}

split_tabs::SplitTabId SplitId(uint64_t value) {
  return split_tabs::SplitTabId::FromRawToken(base::Token(1u, value));
}

ArcImportPlan Plan() {
  const base::Uuid workspace_id = Uuid("10000000-0000-4000-8000-000000000001");
  const base::Uuid folder_id = Uuid("20000000-0000-4000-8000-000000000001");
  const base::Uuid first_id = Uuid("30000000-0000-4000-8000-000000000001");
  const base::Uuid second_id = Uuid("30000000-0000-4000-8000-000000000002");

  tab_tree::Workspace workspace;
  workspace.id = workspace_id;
  workspace.name = u"Private workspace";

  tab_tree::TreeNode folder;
  folder.id = folder_id;
  folder.workspace_id = workspace_id;
  folder.type = tab_tree::TreeNodeType::kFolder;
  folder.title = u"Private split title";

  tab_tree::TreeNode first;
  first.id = first_id;
  first.workspace_id = workspace_id;
  first.parent_id = folder_id;
  first.type = tab_tree::TreeNodeType::kSavedPage;
  first.title = u"Private first title";
  first.url = GURL("https://private-first.example/path");

  tab_tree::TreeNode second = first;
  second.id = second_id;
  second.title = u"Private second title";
  second.url = GURL("https://private-second.example/path");

  ArcImportPlan plan;
  plan.tree.workspaces.push_back(std::move(workspace));
  plan.tree.nodes = {std::move(folder), std::move(first), std::move(second)};
  plan.splits.push_back({
      .folder_node_id = folder_id,
      .member_node_ids = {first_id, second_id},
      .orientation = ArcSplitOrientation::kHorizontal,
      .focused_member_node_id = second_id,
      .normalized_ratios = {0.4, 0.6},
  });
  return plan;
}

std::unique_ptr<sessions::SessionTab> SessionTabFor(
    const ArcImportPlan& plan,
    size_t member_index,
    SessionID window_id,
    split_tabs::SplitTabId split_id) {
  const ArcSplitDescriptor& split = plan.splits.front();
  const base::Uuid& member_id = split.member_node_ids[member_index];
  const auto node_it =
      std::ranges::find(plan.tree.nodes, member_id, &tab_tree::TreeNode::id);
  auto tab = std::make_unique<sessions::SessionTab>();
  tab->window_id = window_id;
  tab->tab_id = SessionId(101 + static_cast<int>(member_index));
  tab->tab_visual_index = static_cast<int>(member_index);
  tab->split_id = split_id;
  const std::optional<std::string> metadata = session::EncodeTabSessionMetadata(
      {.workspace_id = node_it->workspace_id, .tree_node_id = member_id});
  tab->extra_data.emplace(session::kTabSessionMetadataExtraDataKey,
                          metadata.value());
  return tab;
}

struct SessionFixture {
  SessionID window_id = SessionID::InvalidValue();
  std::vector<std::unique_ptr<sessions::SessionWindow>> windows;
};

SessionFixture Fixture(const ArcImportPlan& plan,
                       split_tabs::SplitTabId split_id) {
  SessionFixture fixture{.window_id = SessionId(41)};
  auto window = std::make_unique<sessions::SessionWindow>();
  window->window_id = fixture.window_id;
  window->selected_tab_index = 1;
  window->is_constrained = false;
  window->tabs.push_back(SessionTabFor(plan, 0, fixture.window_id, split_id));
  window->tabs.push_back(SessionTabFor(plan, 1, fixture.window_id, split_id));
  auto split = std::make_unique<sessions::SessionSplitTab>(split_id);
  split->split_visual_data_ =
      BuildArcSplitVisualExpectation(plan.splits.front())->visual_data;
  window->split_tabs.push_back(std::move(split));
  fixture.windows.push_back(std::move(window));
  return fixture;
}

ArcSplitReceipt Verify(const ArcImportPlan& plan,
                       const SessionFixture& fixture,
                       bool require_focus = false,
                       SessionID active_window_id = SessionId(41)) {
  return VerifyArcSplitSessionWindows(plan, fixture.window_id, fixture.windows,
                                      active_window_id, require_focus);
}

TEST(ArcSplitReceiptTest, StructureFingerprintIsPrivateAndRuntimeIndependent) {
  const ArcImportPlan original = Plan();
  const std::string fingerprint = ComputeArcSplitStructureFingerprint(original);
  ASSERT_EQ(64u, fingerprint.size());
  EXPECT_EQ(std::string::npos, fingerprint.find("Private"));
  EXPECT_EQ(std::string::npos, fingerprint.find("https://"));

  ArcImportPlan private_fields_changed = original;
  private_fields_changed.tree.nodes[0].title = u"Another private title";
  private_fields_changed.tree.nodes[1].title = u"Another page title";
  private_fields_changed.tree.nodes[1].url =
      GURL("https://different-private.example/");
  EXPECT_EQ(fingerprint,
            ComputeArcSplitStructureFingerprint(private_fields_changed));

  ArcImportPlan source_order_changed = original;
  std::ranges::reverse(source_order_changed.splits[0].member_node_ids);
  std::ranges::reverse(source_order_changed.splits[0].normalized_ratios);
  EXPECT_NE(fingerprint,
            ComputeArcSplitStructureFingerprint(source_order_changed));
}

TEST(ArcSplitReceiptTest, RejectsStructurallyInvalidDescriptors) {
  ArcImportPlan duplicate_member = Plan();
  duplicate_member.splits[0].member_node_ids[1] =
      duplicate_member.splits[0].member_node_ids[0];
  duplicate_member.splits[0].focused_member_node_id =
      duplicate_member.splits[0].member_node_ids[0];
  EXPECT_FALSE(IsValidArcSplitStructure(duplicate_member));
  EXPECT_TRUE(ComputeArcSplitStructureFingerprint(duplicate_member).empty());

  ArcImportPlan wrong_parent = Plan();
  wrong_parent.tree.nodes[1].parent_id.reset();
  EXPECT_FALSE(IsValidArcSplitStructure(wrong_parent));
}

TEST(ArcSplitReceiptTest, VerifiesExactDurableStructureAndFocus) {
  const ArcImportPlan plan = Plan();
  const SessionFixture fixture = Fixture(plan, SplitId(1));
  const ArcSplitReceipt receipt = Verify(plan, fixture, /*require_focus=*/true);

  EXPECT_EQ(ArcSplitReceiptFailure::kNone, receipt.failure);
  EXPECT_EQ(ArcSplitVerification::kExact, receipt.verification);
  EXPECT_EQ(1u, receipt.verified_split_count);
  EXPECT_TRUE(receipt.focus_verified);
  EXPECT_EQ(64u, receipt.structure_sha256.size());
  EXPECT_EQ(64u, receipt.receipt_sha256.size());
}

TEST(ArcSplitReceiptTest, ReceiptDoesNotHashEphemeralSplitId) {
  const ArcImportPlan plan = Plan();
  const SessionFixture first = Fixture(plan, SplitId(1));
  const SessionFixture second = Fixture(plan, SplitId(2));

  const ArcSplitReceipt first_receipt = Verify(plan, first);
  const ArcSplitReceipt second_receipt = Verify(plan, second);
  EXPECT_EQ(ArcSplitReceiptFailure::kNone, first_receipt.failure);
  EXPECT_EQ(ArcSplitReceiptFailure::kNone, second_receipt.failure);
  ASSERT_EQ(ArcSplitVerification::kExact, first_receipt.verification);
  ASSERT_EQ(ArcSplitVerification::kExact, second_receipt.verification);
  EXPECT_EQ(first_receipt.receipt_sha256, second_receipt.receipt_sha256);
}

TEST(ArcSplitReceiptTest, RejectsWrongSourceOrder) {
  const ArcImportPlan plan = Plan();
  SessionFixture fixture = Fixture(plan, SplitId(1));
  std::ranges::reverse(fixture.windows[0]->tabs);

  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, fixture).verification);

  SessionFixture wrong_visual_indices = Fixture(plan, SplitId(1));
  wrong_visual_indices.windows[0]->tabs[0]->tab_visual_index = 1;
  wrong_visual_indices.windows[0]->tabs[1]->tab_visual_index = 0;
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, wrong_visual_indices).verification);
}

TEST(ArcSplitReceiptTest, RejectsExtraOrDifferentlySplitMembers) {
  const ArcImportPlan plan = Plan();
  SessionFixture extra = Fixture(plan, SplitId(1));
  auto extra_tab = std::make_unique<sessions::SessionTab>();
  extra_tab->window_id = extra.window_id;
  extra_tab->tab_id = SessionId(103);
  extra_tab->tab_visual_index = 2;
  extra_tab->split_id = SplitId(1);
  extra.windows[0]->tabs.push_back(std::move(extra_tab));
  EXPECT_EQ(ArcSplitVerification::kConflict, Verify(plan, extra).verification);

  SessionFixture different = Fixture(plan, SplitId(1));
  different.windows[0]->tabs[1]->split_id = SplitId(2);
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, different).verification);
}

TEST(ArcSplitReceiptTest, RejectsDuplicateMetadataAcrossWindows) {
  const ArcImportPlan plan = Plan();
  SessionFixture fixture = Fixture(plan, SplitId(1));
  auto other_window = std::make_unique<sessions::SessionWindow>();
  other_window->window_id = SessionId(42);
  other_window->tabs.push_back(
      SessionTabFor(plan, 0, other_window->window_id, SplitId(3)));
  fixture.windows.push_back(std::move(other_window));

  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, fixture).verification);
}

TEST(ArcSplitReceiptTest, RejectsMissingMetadataOrSplitVisualRecord) {
  const ArcImportPlan plan = Plan();
  SessionFixture missing_metadata = Fixture(plan, SplitId(1));
  missing_metadata.windows[0]->tabs[0]->extra_data.clear();
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, missing_metadata).verification);

  SessionFixture missing_split = Fixture(plan, SplitId(1));
  missing_split.windows[0]->split_tabs.clear();
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, missing_split).verification);

  SessionFixture duplicate_split = Fixture(plan, SplitId(1));
  auto duplicate = std::make_unique<sessions::SessionSplitTab>(SplitId(1));
  duplicate->split_visual_data_ =
      BuildArcSplitVisualExpectation(plan.splits.front())->visual_data;
  duplicate_split.windows[0]->split_tabs.push_back(std::move(duplicate));
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, duplicate_split).verification);

  SessionFixture wrong_visual = Fixture(plan, SplitId(1));
  wrong_visual.windows[0]->split_tabs[0]->split_visual_data_ =
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kStacked, 0.4);
  EXPECT_EQ(ArcSplitVerification::kConflict,
            Verify(plan, wrong_visual).verification);
}

TEST(ArcSplitReceiptTest, ChecksFocusOnlyWhenRequested) {
  const ArcImportPlan plan = Plan();
  SessionFixture fixture = Fixture(plan, SplitId(1));
  fixture.windows[0]->selected_tab_index = 0;

  const ArcSplitReceipt without_focus =
      Verify(plan, fixture, /*require_focus=*/false, SessionID::InvalidValue());
  EXPECT_EQ(ArcSplitReceiptFailure::kNone, without_focus.failure);
  EXPECT_EQ(ArcSplitVerification::kExact, without_focus.verification);
  const ArcSplitReceipt wrong_focus =
      Verify(plan, fixture, /*require_focus=*/true);
  EXPECT_EQ(ArcSplitVerification::kConflict, wrong_focus.verification);
  EXPECT_EQ(ArcSplitReceiptFailure::kFocusMismatch, wrong_focus.failure);

  fixture.windows[0]->selected_tab_index = 1;
  const ArcSplitReceipt exact_focus =
      Verify(plan, fixture, /*require_focus=*/true, SessionId(41));
  EXPECT_EQ(ArcSplitReceiptFailure::kNone, exact_focus.failure);
  EXPECT_EQ(ArcSplitVerification::kExact, exact_focus.verification);
}

TEST(ArcSplitReceiptTest, OtherActiveWindowDoesNotInvalidateTargetMemberFocus) {
  const ArcImportPlan plan = Plan();
  SessionFixture fixture = Fixture(plan, SplitId(1));
  auto other_window = std::make_unique<sessions::SessionWindow>();
  other_window->window_id = SessionId(42);
  other_window->selected_tab_index = 0;
  auto other_tab = std::make_unique<sessions::SessionTab>();
  other_tab->window_id = other_window->window_id;
  other_tab->tab_id = SessionId(201);
  other_tab->tab_visual_index = 0;
  other_window->tabs.push_back(std::move(other_tab));
  fixture.windows.push_back(std::move(other_window));
  const ArcSplitReceipt foreground =
      Verify(plan, fixture, /*require_focus=*/true, fixture.window_id);
  ASSERT_EQ(ArcSplitVerification::kExact, foreground.verification);

  for (SessionID active_window : {SessionId(42), SessionID::InvalidValue()}) {
    const ArcSplitReceipt receipt =
        Verify(plan, fixture, /*require_focus=*/true, active_window);
    EXPECT_EQ(ArcSplitVerification::kExact, receipt.verification);
    EXPECT_EQ(ArcSplitReceiptFailure::kNone, receipt.failure);
    EXPECT_TRUE(receipt.focus_verified);
    EXPECT_EQ(foreground.receipt_sha256, receipt.receipt_sha256);
  }

  // Changing application/window activation is harmless; changing the selected
  // pane inside the actual target window is still a failed focus receipt.
  fixture.windows[0]->selected_tab_index = 0;
  const ArcSplitReceipt wrong_member =
      Verify(plan, fixture, /*require_focus=*/true, SessionId(42));
  EXPECT_EQ(ArcSplitVerification::kConflict, wrong_member.verification);
  EXPECT_EQ(ArcSplitReceiptFailure::kFocusMismatch, wrong_member.failure);
  EXPECT_FALSE(wrong_member.focus_verified);
  EXPECT_TRUE(wrong_member.receipt_sha256.empty());

  fixture.windows[0]->selected_tab_index = -1;
  const ArcSplitReceipt missing_selection =
      Verify(plan, fixture, /*require_focus=*/true, SessionId(42));
  EXPECT_EQ(ArcSplitVerification::kConflict, missing_selection.verification);
  EXPECT_EQ(ArcSplitReceiptFailure::kInvalidFocusContext,
            missing_selection.failure);
}

TEST(ArcSplitReceiptTest, OtherActiveWindowDoesNotRelaxMemberWindowOwnership) {
  const ArcImportPlan plan = Plan();
  SessionFixture fixture = Fixture(plan, SplitId(1));
  fixture.windows[0]->tabs[1]->window_id = SessionId(42);
  const ArcSplitReceipt receipt =
      Verify(plan, fixture, /*require_focus=*/true, SessionId(42));
  EXPECT_EQ(ArcSplitVerification::kConflict, receipt.verification);
  EXPECT_EQ(ArcSplitReceiptFailure::kInvalidMember, receipt.failure);
  EXPECT_FALSE(receipt.focus_verified);
  EXPECT_TRUE(receipt.receipt_sha256.empty());
}

}  // namespace

}  // namespace ahoi::importer::arc
