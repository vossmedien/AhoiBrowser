// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tests for the explicit folders-as-workspaces Arc import layout. The fixture
// is synthetic: one space whose pinned container holds loose tabs and two
// top-level folders, one of them with a nested folder and a split view.

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "ahoi/browser/importer/arc/arc_import_transaction.h"
#include "ahoi/browser/importer/arc/arc_import_transaction_key.h"
#include "ahoi/browser/importer/arc/arc_import_unittest_support.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

using test_support::SnapshotFor;

constexpr ArcImportPlanOptions kFolderLayout{.folders_as_workspaces = true};

base::ListValue Ids(std::vector<std::string_view> ids) {
  base::ListValue list;
  for (std::string_view id : ids) {
    list.Append(id);
  }
  return list;
}

base::DictValue Tab(std::string_view url) {
  return base::DictValue().Set(
      "tab", base::DictValue().Set("savedURL", url).Set("savedTitle", url));
}

base::DictValue List() {
  return base::DictValue().Set("list", base::DictValue());
}

base::DictValue Container() {
  return base::DictValue().Set(
      "itemContainer",
      base::DictValue().Set(
          "containerType",
          base::DictValue().Set("spaceItems",
                                base::DictValue().Set("_0", "space-crew"))));
}

base::DictValue Split() {
  return base::DictValue().Set(
      "splitView", base::DictValue()
                       .Set("layoutOrientation", "horizontal")
                       .Set("focusItemID", "split-right")
                       .Set("itemWidthFactors", base::ListValue()
                                                    .Append("split-left")
                                                    .Append(0.25)
                                                    .Append("split-right")
                                                    .Append(0.75)));
}

void AddItem(base::ListValue* items,
             std::string_view id,
             std::optional<std::string_view> parent,
             std::vector<std::string_view> children,
             std::optional<std::string_view> title,
             base::DictValue data) {
  base::DictValue value;
  value.Set("id", id);
  value.Set("parentID", parent ? base::Value(*parent) : base::Value());
  value.Set("childrenIds", Ids(std::move(children)));
  value.Set("title", title ? base::Value(*title) : base::Value());
  value.Set("data", std::move(data));
  items->Append(id);
  items->Append(std::move(value));
}

// Space "Crew": pinned [loose-a, Alpha, Beta, loose-b], unpinned [temp].
// Alpha holds a page, a nested folder and a split; Beta holds one page.
// Without loose items, the pinned root holds only the two folders.
std::string CrewSidebar(bool with_loose_items) {
  base::ListValue items;
  const std::vector<std::string_view> pinned =
      with_loose_items ? std::vector<std::string_view>{"tab-loose-a",
                                                       "folder-alpha",
                                                       "folder-beta",
                                                       "tab-loose-b"}
                       : std::vector<std::string_view>{"folder-alpha",
                                                       "folder-beta"};
  AddItem(&items, "root-pinned", std::nullopt, pinned, std::nullopt,
          Container());
  AddItem(&items, "root-unpinned", std::nullopt,
          with_loose_items ? std::vector<std::string_view>{"tab-temp"}
                           : std::vector<std::string_view>{},
          std::nullopt, Container());
  if (with_loose_items) {
    AddItem(&items, "tab-loose-a", "root-pinned", {}, "Loose A",
            Tab("https://loose-a.example.test/"));
    AddItem(&items, "tab-loose-b", "root-pinned", {}, "Loose B",
            Tab("https://loose-b.example.test/"));
    AddItem(&items, "tab-temp", "root-unpinned", {}, "Temporary",
            Tab("https://temp.example.test/"));
  }
  AddItem(&items, "folder-alpha", "root-pinned",
          {"tab-alpha", "folder-nested", "split-alpha"}, "Alpha", List());
  AddItem(&items, "tab-alpha", "folder-alpha", {}, "Alpha page",
          Tab("https://alpha.example.test/"));
  AddItem(&items, "folder-nested", "folder-alpha", {"tab-nested"}, "Nested",
          List());
  AddItem(&items, "tab-nested", "folder-nested", {}, "Nested page",
          Tab("https://nested.example.test/"));
  AddItem(&items, "split-alpha", "folder-alpha", {"split-left", "split-right"},
          std::nullopt, Split());
  AddItem(&items, "split-left", "split-alpha", {}, "Left",
          Tab("https://left.example.test/"));
  AddItem(&items, "split-right", "split-alpha", {}, "Right",
          Tab("https://right.example.test/"));
  AddItem(&items, "folder-beta", "root-pinned", {"tab-beta"}, "Beta", List());
  AddItem(&items, "tab-beta", "folder-beta", {}, "Beta page",
          Tab("https://beta.example.test/"));

  base::DictValue space;
  space.Set("id", "space-crew");
  space.Set("title", "Crew");
  space.Set("containerIDs", base::ListValue());
  space.Set("newContainerIDs",
            base::ListValue()
                .Append(base::DictValue().Set("pinned", base::DictValue()))
                .Append("root-pinned")
                .Append(base::DictValue().Set(
                    "unpinned",
                    base::DictValue().Set(
                        "_0", base::DictValue().Set("shared",
                                                    base::DictValue()))))
                .Append("root-unpinned"));
  base::DictValue root;
  root.Set("version", 1);
  root.Set(
      "sidebar",
      base::DictValue().Set(
          "containers",
          base::ListValue()
              .Append(base::DictValue().Set("global", base::DictValue()))
              .Append(base::DictValue()
                          .Set("spaces", base::ListValue()
                                             .Append("space-crew")
                                             .Append(std::move(space)))
                          .Set("items", std::move(items)))));
  std::optional<std::string> json = base::WriteJson(root);
  return json.value_or(std::string());
}

ArcImportPlan ParseCrew(const ArcImportPlanOptions& options,
                        bool with_loose_items = true) {
  ArcParseResult parsed =
      ParseArcSnapshot(SnapshotFor(CrewSidebar(with_loose_items)), options);
  EXPECT_EQ(ArcImportStatus::kOk, parsed.status);
  return parsed.plan.value_or(ArcImportPlan());
}

base::Uuid ItemId(std::string_view source_id) {
  return MakeDeterministicArcId(internal::kFolderLayoutItemIdDomain,
                                source_id);
}

const tab_tree::TreeNode* FindNode(const ArcImportPlan& plan,
                                   const base::Uuid& id) {
  for (const tab_tree::TreeNode& node : plan.tree.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

std::vector<std::u16string> WorkspaceNames(const ArcImportPlan& plan) {
  std::vector<std::u16string> names;
  for (const tab_tree::Workspace& workspace : plan.tree.workspaces) {
    names.push_back(workspace.name);
  }
  return names;
}

TEST(ArcImportFolderWorkspacesTest, OptionOffKeepsTheSpaceLayout) {
  const ArcImportPlan plan = ParseCrew(ArcImportPlanOptions());
  const ArcParseResult defaulted =
      ParseArcSnapshot(SnapshotFor(CrewSidebar(/*with_loose_items=*/true)));

  ASSERT_TRUE(defaulted.plan.has_value());
  EXPECT_EQ(*defaulted.plan, plan);
  EXPECT_FALSE(plan.options.folders_as_workspaces);
  EXPECT_EQ(std::vector<std::u16string>({u"Crew"}), WorkspaceNames(plan));
  EXPECT_EQ(1u, plan.stats.imported_workspace_count);
  EXPECT_EQ(4u, plan.stats.imported_folder_count);
  EXPECT_EQ(8u, plan.stats.imported_page_count);
  EXPECT_EQ(1u, plan.stats.imported_split_count);
  // The option is offered because two pinned top-level folders exist.
  EXPECT_EQ(2u, plan.stats.source_top_level_folder_count);
  EXPECT_EQ(0u, plan.stats.folder_workspace_count);
  EXPECT_EQ(MakeDeterministicArcId(internal::kWorkspaceIdDomain, "space-crew"),
            plan.tree.workspaces.front().id);
}

TEST(ArcImportFolderWorkspacesTest, OptionOnPromotesTopLevelPinnedFolders) {
  const ArcImportPlan plan = ParseCrew(kFolderLayout);

  EXPECT_TRUE(plan.options.folders_as_workspaces);
  ASSERT_EQ(std::vector<std::u16string>({u"Crew", u"Alpha", u"Beta"}),
            WorkspaceNames(plan));
  EXPECT_EQ(3u, plan.stats.imported_workspace_count);
  EXPECT_EQ(2u, plan.stats.folder_workspace_count);
  EXPECT_EQ(2u, plan.stats.source_top_level_folder_count);
  EXPECT_EQ(2u, plan.stats.imported_folder_count);
  EXPECT_EQ(8u, plan.stats.imported_page_count);
  EXPECT_EQ(1u, plan.stats.imported_split_count);
  const base::Uuid crew = plan.tree.workspaces[0].id;
  const base::Uuid alpha = plan.tree.workspaces[1].id;
  const base::Uuid beta = plan.tree.workspaces[2].id;
  EXPECT_EQ(MakeDeterministicArcId(internal::kFolderWorkspaceIdDomain,
                                   "folder-alpha"),
            alpha);
  EXPECT_LT(plan.tree.workspaces[0].sort_key, plan.tree.workspaces[1].sort_key);
  EXPECT_LT(plan.tree.workspaces[1].sort_key, plan.tree.workspaces[2].sort_key);

  // Promoted folders are workspaces, never nodes.
  EXPECT_FALSE(FindNode(plan, ItemId("folder-alpha")));
  EXPECT_FALSE(FindNode(plan, ItemId("folder-beta")));

  // Loose pinned tabs keep their order; unpinned tabs follow as before.
  const struct {
    const char* id;
    base::Uuid workspace;
    std::optional<base::Uuid> parent;
    const char* sort_key;
  } kExpected[] = {
      {"tab-loose-a", crew, std::nullopt, "0000000000"},
      {"tab-loose-b", crew, std::nullopt, "0000000001"},
      {"tab-temp", crew, std::nullopt, "0000000002"},
      {"tab-alpha", alpha, std::nullopt, "0000000000"},
      {"folder-nested", alpha, std::nullopt, "0000000001"},
      {"tab-nested", alpha, ItemId("folder-nested"), "0000000000"},
      {"split-alpha", alpha, std::nullopt, "0000000002"},
      {"split-left", alpha, ItemId("split-alpha"), "0000000000"},
      {"split-right", alpha, ItemId("split-alpha"), "0000000001"},
      {"tab-beta", beta, std::nullopt, "0000000000"},
  };
  for (const auto& expected : kExpected) {
    SCOPED_TRACE(expected.id);
    const tab_tree::TreeNode* node = FindNode(plan, ItemId(expected.id));
    ASSERT_TRUE(node);
    EXPECT_EQ(expected.workspace, node->workspace_id);
    EXPECT_EQ(expected.parent, node->parent_id);
    EXPECT_EQ(expected.sort_key, node->sort_key);
  }

  ASSERT_EQ(1u, plan.splits.size());
  const ArcSplitDescriptor& split = plan.splits.front();
  EXPECT_EQ(ItemId("split-alpha"), split.folder_node_id);
  EXPECT_EQ(std::vector<base::Uuid>({ItemId("split-left"),
                                     ItemId("split-right")}),
            split.member_node_ids);
  EXPECT_EQ(ItemId("split-right"), split.focused_member_node_id);
  EXPECT_EQ(std::vector<double>({0.25, 0.75}), split.normalized_ratios);
}

TEST(ArcImportFolderWorkspacesTest, LayoutsAreDeterministicAndDisjoint) {
  const ArcImportPlan first = ParseCrew(kFolderLayout);
  const ArcImportPlan second = ParseCrew(kFolderLayout);
  EXPECT_EQ(first, second);

  const ArcImportPlan spaces = ParseCrew(ArcImportPlanOptions());
  std::set<base::Uuid> space_ids;
  for (const tab_tree::Workspace& workspace : spaces.tree.workspaces) {
    space_ids.insert(workspace.id);
  }
  for (const tab_tree::TreeNode& node : spaces.tree.nodes) {
    space_ids.insert(node.id);
  }
  for (const tab_tree::Workspace& workspace : first.tree.workspaces) {
    EXPECT_FALSE(space_ids.contains(workspace.id));
  }
  for (const tab_tree::TreeNode& node : first.tree.nodes) {
    EXPECT_FALSE(space_ids.contains(node.id));
  }
}

TEST(ArcImportFolderWorkspacesTest, EmptySpaceWorkspaceIsNotCreated) {
  const ArcImportPlan plan =
      ParseCrew(kFolderLayout, /*with_loose_items=*/false);

  EXPECT_EQ(std::vector<std::u16string>({u"Alpha", u"Beta"}),
            WorkspaceNames(plan));
  EXPECT_EQ(2u, plan.stats.imported_workspace_count);
  EXPECT_EQ(2u, plan.stats.folder_workspace_count);

  const ArcImportPlan spaces =
      ParseCrew(ArcImportPlanOptions(), /*with_loose_items=*/false);
  EXPECT_EQ(std::vector<std::u16string>({u"Crew"}), WorkspaceNames(spaces));
}

TEST(ArcImportFolderWorkspacesTest, CommitIsAtomicAndRepeatIsNoOp) {
  const ArcImportPlan plan = ParseCrew(kFolderLayout);
  const ArcImportMergeResult first = MergeArcImportPlan(
      tab_tree::TabTreeSnapshot(), plan, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, first.status);
  ASSERT_TRUE(first.merged_tree.has_value());
  ASSERT_TRUE(first.applied_plan.has_value());
  EXPECT_EQ(3u, first.merged_tree->workspaces.size());
  EXPECT_EQ(3u, first.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(1u, first.applied_plan->splits.size());

  const ArcImportMergeResult second = MergeArcImportPlan(
      *first.merged_tree, ParseCrew(kFolderLayout),
      ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kNoChanges, second.status);
  EXPECT_FALSE(second.changed);
  ASSERT_TRUE(second.merged_tree.has_value());
  EXPECT_EQ(*first.merged_tree, *second.merged_tree);
  ASSERT_TRUE(second.applied_plan.has_value());
  EXPECT_EQ(0u, second.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(3u, second.applied_plan->stats.deduplicated_workspace_count);
}

TEST(ArcImportFolderWorkspacesTest, LayoutChangeUsesConflictPolicyNotIds) {
  const ArcImportMergeResult spaces = MergeArcImportPlan(
      tab_tree::TabTreeSnapshot(), ParseCrew(ArcImportPlanOptions()),
      ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, spaces.status);
  ASSERT_TRUE(spaces.merged_tree.has_value());

  const ArcImportMergeResult renamed =
      MergeArcImportPlan(*spaces.merged_tree, ParseCrew(kFolderLayout),
                         ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, renamed.status);
  EXPECT_EQ(1u, renamed.renamed_workspace_count);
  ASSERT_TRUE(renamed.merged_tree.has_value());
  EXPECT_EQ(4u, renamed.merged_tree->workspaces.size());

  const ArcImportMergeResult skipped =
      MergeArcImportPlan(*spaces.merged_tree, ParseCrew(kFolderLayout),
                         ArcConflictResolution::kSkip);
  ASSERT_EQ(ArcImportStatus::kOk, skipped.status);
  EXPECT_EQ(1u, skipped.skipped_workspace_count);
  ASSERT_TRUE(skipped.applied_plan.has_value());
  EXPECT_EQ(2u, skipped.applied_plan->stats.imported_workspace_count);
  EXPECT_EQ(1u, skipped.applied_plan->splits.size());
}

TEST(ArcImportFolderWorkspacesTest, OptionIsPartOfTheTransactionKey) {
  ArcImportTransactionSelection spaces{
      .selected_browser_profiles = {"Default"}};
  ArcImportTransactionSelection folders = spaces;
  folders.folders_as_workspaces = true;

  const std::string spaces_fingerprint =
      ComputeArcImportSelectionFingerprint(spaces);
  const std::string folders_fingerprint =
      ComputeArcImportSelectionFingerprint(folders);
  EXPECT_NE(spaces_fingerprint, folders_fingerprint);
  EXPECT_EQ(folders_fingerprint, ComputeArcImportSelectionFingerprint(folders));
  constexpr char kSnapshot[] =
      "2222222222222222222222222222222222222222222222222222222222222222";
  EXPECT_NE(ComputeArcImportIdempotencyKey(kSnapshot, spaces_fingerprint),
            ComputeArcImportIdempotencyKey(kSnapshot, folders_fingerprint));
}

}  // namespace

}  // namespace ahoi::importer::arc
