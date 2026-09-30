// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Parser tests for the Arc StorableSidebar.json snapshot: deterministic
// detached plans, container-map forms, splits and rejection of malformed
// input (split from arc_import_unittest.cc, source line budget).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "ahoi/browser/importer/arc/arc_import_unittest_support.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

using test_support::kValidArcSidebar;
using test_support::ReplaceOnce;
using test_support::SnapshotFor;

TEST(ArcImportParserTest, BuildsDeterministicDetachedPlan) {
  const ArcImportSnapshot snapshot = SnapshotFor(kValidArcSidebar);
  const ArcParseResult first = ParseArcSnapshot(snapshot);
  const ArcParseResult second = ParseArcSnapshot(snapshot);

  ASSERT_EQ(ArcImportStatus::kOk, first.status);
  ASSERT_EQ(ArcImportStatus::kOk, second.status);
  ASSERT_TRUE(first.plan.has_value());
  ASSERT_TRUE(second.plan.has_value());
  EXPECT_EQ(*first.plan, *second.plan);
  EXPECT_EQ(kArcImportPlanSchemaVersion, first.plan->schema_version);
  ASSERT_EQ(1u, first.plan->tree.workspaces.size());
  EXPECT_EQ(6u, first.plan->tree.nodes.size());
  EXPECT_EQ(1u, first.plan->stats.imported_workspace_count);
  EXPECT_EQ(2u, first.plan->stats.imported_folder_count);
  EXPECT_EQ(4u, first.plan->stats.imported_page_count);
  EXPECT_EQ(1u, first.plan->stats.imported_split_count);
  EXPECT_EQ(0u, first.plan->stats.degraded_split_count);
  EXPECT_EQ(2u, first.plan->stats.skipped_unsafe_url_count);
  EXPECT_EQ(1u, first.plan->stats.skipped_unsupported_item_count);
  EXPECT_EQ(0u, first.plan->stats.ignored_unreachable_item_count);
  ASSERT_EQ(1u, first.plan->splits.size());
  EXPECT_EQ(2u, first.plan->splits.front().member_node_ids.size());
  EXPECT_EQ(ArcSplitOrientation::kHorizontal,
            first.plan->splits.front().orientation);
  EXPECT_EQ(std::vector<double>({0.5, 0.5}),
            first.plan->splits.front().normalized_ratios);

  const std::string workspace_id =
      first.plan->tree.workspaces.front().id.AsLowercaseString();
  ASSERT_EQ(36u, workspace_id.size());
  EXPECT_EQ('5', workspace_id[14]);
  EXPECT_NE(std::string::npos, std::string("89ab").find(workspace_id[19]));
  EXPECT_TRUE(first.plan->tree.undo_operations.empty());
}

TEST(ArcImportParserTest, CurrentContainerMapOrderIsNotSemantic) {
  const ArcParseResult unpinned_first =
      ParseArcSnapshot(SnapshotFor(kValidArcSidebar));
  std::string pinned_first_json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(
      &pinned_first_json,
      R"json({"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned",
            {"pinned": {}},
            "root-pinned")json",
      R"json({"pinned": {}},
            "root-pinned",
            {"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned")json"));
  const ArcParseResult pinned_first =
      ParseArcSnapshot(SnapshotFor(std::move(pinned_first_json)));

  ASSERT_EQ(ArcImportStatus::kOk, unpinned_first.status);
  ASSERT_EQ(ArcImportStatus::kOk, pinned_first.status);
  ASSERT_TRUE(unpinned_first.plan.has_value());
  ASSERT_TRUE(pinned_first.plan.has_value());
  EXPECT_EQ(*unpinned_first.plan, *pinned_first.plan);
  ASSERT_FALSE(unpinned_first.plan->tree.nodes.empty());
  EXPECT_EQ(u"Pinned page", unpinned_first.plan->tree.nodes.front().title);
}

TEST(ArcImportParserTest, SupportsLegacyContainerMapPairs) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(
      &json,
      R"json("containerIDs": [],
          "newContainerIDs": [
            {"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned",
            {"pinned": {}},
            "root-pinned"
          ])json",
      R"json("containerIDs": [
            "unpinned", "root-unpinned",
            "pinned", "root-pinned"
          ])json"));

  const ArcParseResult parsed = ParseArcSnapshot(SnapshotFor(std::move(json)));
  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  ASSERT_TRUE(parsed.plan.has_value());
  EXPECT_EQ(1u, parsed.plan->stats.imported_workspace_count);
  EXPECT_EQ(4u, parsed.plan->stats.imported_page_count);
  ASSERT_FALSE(parsed.plan->tree.nodes.empty());
  EXPECT_EQ(u"Pinned page", parsed.plan->tree.nodes.front().title);
}

TEST(ArcImportParserTest, RejectsNonListCurrentMapInsteadOfFallingBack) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(
      &json,
      R"json("containerIDs": [],
          "newContainerIDs": [
            {"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned",
            {"pinned": {}},
            "root-pinned"
          ])json",
      R"json("containerIDs": [
            "unpinned", "root-unpinned",
            "pinned", "root-pinned"
          ],
          "newContainerIDs": {"unexpected": true})json"));

  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsUnknownCurrentContainerSelector) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(
      &json, R"json({"pinned": {}},
            "root-pinned")json",
      R"json({"archived": {}},
            "root-pinned")json"));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsNonStringCurrentContainerId) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, R"json("root-unpinned",
            {"pinned": {}})json",
                          R"json({"unexpected": true},
            {"pinned": {}})json"));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsDuplicateCurrentContainerKind) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json,
                          R"json({"unpinned": {"_0": {"shared": {}}}})json",
                          R"json({"pinned": {}})json"));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsDuplicateCurrentContainerId) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, R"json("root-unpinned",
            {"pinned": {}})json",
                          R"json("root-pinned",
            {"pinned": {}})json"));
  EXPECT_EQ(ArcImportStatus::kDuplicateIdentifier,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, DomainSeparatesDeterministicIds) {
  const base::Uuid first = MakeDeterministicArcId("workspace", "same-source");
  const base::Uuid repeat = MakeDeterministicArcId("workspace", "same-source");
  const base::Uuid other = MakeDeterministicArcId("item", "same-source");

  EXPECT_TRUE(first.is_valid());
  EXPECT_EQ(first, repeat);
  EXPECT_NE(first, other);
  EXPECT_FALSE(MakeDeterministicArcId("", "same-source").is_valid());
}

TEST(ArcImportParserTest, RejectsMutationAfterSnapshot) {
  ArcImportSnapshot snapshot = SnapshotFor(kValidArcSidebar);
  snapshot.json.push_back(' ');
  EXPECT_EQ(ArcImportStatus::kSourceChanged, ParseArcSnapshot(snapshot).status);
}

TEST(ArcImportParserTest, RejectsUnsupportedSchema) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, "\"version\": 1", "\"version\": 2"));
  EXPECT_EQ(ArcImportStatus::kUnsupportedSchema,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, InfersAndNormalizesPartialSplitFactors) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json,
                          R"json("itemWidthFactors": [
                "split-tab-a", 0.5,
                "split-tab-b", 0.5
              ])json",
                          R"json("itemWidthFactors": [
                "split-tab-a", 0.25
              ])json"));

  const ArcParseResult parsed = ParseArcSnapshot(SnapshotFor(std::move(json)));

  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  ASSERT_TRUE(parsed.plan.has_value());
  ASSERT_EQ(1u, parsed.plan->splits.size());
  ASSERT_EQ(2u, parsed.plan->splits.front().normalized_ratios.size());
  EXPECT_DOUBLE_EQ(0.25, parsed.plan->splits.front().normalized_ratios[0]);
  EXPECT_DOUBLE_EQ(0.75, parsed.plan->splits.front().normalized_ratios[1]);
}

TEST(ArcImportParserTest, InvalidSplitDegradesWithoutPhantomPages) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, R"json("focusItemID": "split-tab-b")json",
                          R"json("focusItemID": "not-a-child")json"));

  const ArcParseResult parsed = ParseArcSnapshot(SnapshotFor(std::move(json)));

  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  ASSERT_TRUE(parsed.plan.has_value());
  EXPECT_TRUE(parsed.plan->splits.empty());
  EXPECT_EQ(0u, parsed.plan->stats.imported_split_count);
  EXPECT_EQ(1u, parsed.plan->stats.degraded_split_count);
  ASSERT_EQ(1u, parsed.plan->degraded_split_folder_node_ids.size());
  const base::Uuid degraded_id =
      parsed.plan->degraded_split_folder_node_ids.front();
  const auto degraded = std::ranges::find(parsed.plan->tree.nodes, degraded_id,
                                          &tab_tree::TreeNode::id);
  ASSERT_NE(parsed.plan->tree.nodes.end(), degraded);
  EXPECT_EQ(tab_tree::TreeNodeType::kFolder, degraded->type);
  EXPECT_EQ(4u, parsed.plan->stats.imported_page_count);
  EXPECT_EQ(6u, parsed.plan->tree.nodes.size());
}

TEST(ArcImportParserTest, GlobalTopAppPagesCarryExplicitSemanticMarkers) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, R"json("childrenIds": ["tab-a"])json",
                          R"json("childrenIds": [])json"));
  ASSERT_TRUE(ReplaceOnce(&json,
                          R"json("id": "topapps-root",
          "parentID": null,
          "childrenIds": [])json",
                          R"json("id": "topapps-root",
          "parentID": null,
          "childrenIds": ["tab-a"])json"));
  ASSERT_TRUE(ReplaceOnce(&json, R"json("parentID": "root-pinned")json",
                          R"json("parentID": "topapps-root")json"));

  const ArcParseResult parsed = ParseArcSnapshot(SnapshotFor(std::move(json)));

  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  ASSERT_TRUE(parsed.plan.has_value());
  EXPECT_EQ(1u, parsed.plan->stats.imported_global_top_app_count);
  ASSERT_EQ(1u, parsed.plan->global_top_app_page_node_ids.size());
  const base::Uuid top_app_id =
      parsed.plan->global_top_app_page_node_ids.front();
  const auto top_app = std::ranges::find(parsed.plan->tree.nodes, top_app_id,
                                         &tab_tree::TreeNode::id);
  ASSERT_NE(parsed.plan->tree.nodes.end(), top_app);
  EXPECT_EQ(tab_tree::TreeNodeType::kSavedPage, top_app->type);
  EXPECT_EQ(u"Pinned page", top_app->title);
}

TEST(ArcImportParserTest, RejectsMalformedSerializedMap) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, "\"spaces\": [",
                          "\"spaces\": [\"dangling\","));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsDuplicateIdentifiers) {
  std::string json = kValidArcSidebar;
  size_t offset = 0;
  while ((offset = json.find("topapps-root", offset)) != std::string::npos) {
    json.replace(offset, std::string_view("topapps-root").size(),
                 "root-pinned");
    offset += std::string_view("root-pinned").size();
  }
  EXPECT_EQ(ArcImportStatus::kDuplicateIdentifier,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsParentChildDisagreement) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, "\"parentID\": \"folder-a\"",
                          "\"parentID\": \"root-unpinned\""));
  EXPECT_EQ(ArcImportStatus::kGraphViolation,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

TEST(ArcImportParserTest, RejectsOversizedText) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, "\"title\": \"Work\"",
                          std::string("\"title\": \"") +
                              std::string(kMaxTitleBytes + 1, 'w') + "\""));
  EXPECT_NE(ArcImportStatus::kOk,
            ParseArcSnapshot(SnapshotFor(std::move(json))).status);
}

// ADR 0011 WS-ISO-10: the owning Arc profile of every space.
std::optional<ArcImportPlan> ParseWithSpaceProfile(std::string_view profile) {
  std::string json = kValidArcSidebar;
  if (!ReplaceOnce(&json, "\"title\": \"Work\",",
                   std::string("\"title\": \"Work\", \"profile\": ") +
                       std::string(profile) + ",")) {
    return std::nullopt;
  }
  return ParseArcSnapshot(SnapshotFor(std::move(json))).plan;
}

std::string OnlyWorkspaceProfile(const ArcImportPlan& plan) {
  if (plan.tree.workspaces.size() != 1u ||
      plan.workspace_arc_profiles.size() != 1u) {
    return std::string();
  }
  return plan.workspace_arc_profiles.at(plan.tree.workspaces.front().id);
}

TEST(ArcImportParserTest, ReadsTheCustomArcProfileOfASpace) {
  const std::optional<ArcImportPlan> plan = ParseWithSpaceProfile(
      R"json({"custom": {"_0": {"directoryBasename": "Profile 1",
                                 "machineID": "fixture-machine"}}})json");
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ("Profile 1", OnlyWorkspaceProfile(*plan));
  EXPECT_EQ((std::vector<ArcImportProfileSpaces>{
                {.directory_name = "Profile 1", .space_titles = {"Work"}}}),
            plan->arc_profiles);
}

TEST(ArcImportParserTest, ReadsTheDefaultArcProfileOfASpace) {
  const std::optional<ArcImportPlan> plan =
      ParseWithSpaceProfile(R"json({"default": true})json");
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(kArcDefaultProfileName, OnlyWorkspaceProfile(*plan));
  EXPECT_EQ((std::vector<ArcImportProfileSpaces>{
                {.directory_name = kArcDefaultProfileName,
                 .space_titles = {"Work"}}}),
            plan->arc_profiles);
}

TEST(ArcImportParserTest, MissingOrUnknownArcProfileMeansDefault) {
  // The base fixture carries no profile at all.
  const ArcParseResult missing =
      ParseArcSnapshot(SnapshotFor(kValidArcSidebar));
  ASSERT_TRUE(missing.plan.has_value());
  EXPECT_EQ(kArcDefaultProfileName, OnlyWorkspaceProfile(*missing.plan));

  for (std::string_view unknown : {
           R"json(null)json",
           R"json("Profile 1")json",
           R"json({"custom": 5})json",
           R"json({"custom": {"_0": {}}})json",
           R"json({"custom": {"_0": {"directoryBasename": 7}}})json",
           R"json({"custom": {"_0": {"directoryBasename": ""}}})json",
           R"json({"custom": {"_0": {"directoryBasename": "a/b"}}})json",
           R"json({"custom": {"_0": {"directoryBasename": ".."}}})json",
           R"json({"shared": {"_0": {"directoryBasename": "P"}}})json",
       }) {
    SCOPED_TRACE(unknown);
    const std::optional<ArcImportPlan> plan = ParseWithSpaceProfile(unknown);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(kArcDefaultProfileName, OnlyWorkspaceProfile(*plan));
  }
}

TEST(ArcImportParserTest, ArcProfileDoesNotChangePlanIdentities) {
  const std::optional<ArcImportPlan> custom = ParseWithSpaceProfile(
      R"json({"custom": {"_0": {"directoryBasename": "Profile 3"}}})json");
  const ArcParseResult plain =
      ParseArcSnapshot(SnapshotFor(kValidArcSidebar));
  ASSERT_TRUE(custom.has_value());
  ASSERT_TRUE(plain.plan.has_value());
  EXPECT_EQ(plain.plan->tree, custom->tree);
  EXPECT_EQ(plain.plan->splits, custom->splits);
}

}  // namespace

}  // namespace ahoi::importer::arc
