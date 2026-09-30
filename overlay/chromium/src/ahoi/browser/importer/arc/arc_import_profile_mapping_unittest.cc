// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// ADR 0011 WS-ISO-10: mapping Arc profiles to fully separated Workspaces.
// The synthetic fixture has three Arc profiles: Default owns "Home",
// "Profile 1" owns "Work" (a folder and a split) and "Side", and "Profile 2"
// owns "Solo".

#include "ahoi/browser/importer/arc/arc_import_profile_mapping.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "ahoi/browser/importer/arc/arc_import_transaction.h"
#include "ahoi/browser/importer/arc/arc_import_unittest_support.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

using test_support::SnapshotFor;

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

base::DictValue Container(std::string_view space_id) {
  return base::DictValue().Set(
      "itemContainer",
      base::DictValue().Set(
          "containerType",
          base::DictValue().Set("spaceItems",
                                base::DictValue().Set("_0", space_id))));
}

base::DictValue Split() {
  return base::DictValue().Set(
      "splitView", base::DictValue()
                       .Set("layoutOrientation", "vertical")
                       .Set("focusItemID", "tab-s1")
                       .Set("itemWidthFactors", base::ListValue()
                                                    .Append("tab-s1")
                                                    .Append(0.5)
                                                    .Append("tab-s2")
                                                    .Append(0.5)));
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

base::DictValue CustomProfile(std::string_view directory) {
  return base::DictValue().Set(
      "custom",
      base::DictValue().Set("_0", base::DictValue()
                                      .Set("directoryBasename", directory)
                                      .Set("machineID", "fixture-machine")));
}

// Adds a space with roots "<id>-pinned" and "<id>-unpinned".
void AddSpace(base::ListValue* spaces,
              base::ListValue* items,
              std::string_view id,
              std::string_view title,
              base::DictValue profile,
              std::vector<std::string_view> pinned,
              std::vector<std::string_view> unpinned) {
  const std::string pinned_root = std::string(id) + "-pinned";
  const std::string unpinned_root = std::string(id) + "-unpinned";
  AddItem(items, pinned_root, std::nullopt, std::move(pinned), std::nullopt,
          Container(id));
  AddItem(items, unpinned_root, std::nullopt, std::move(unpinned),
          std::nullopt, Container(id));
  base::DictValue space;
  space.Set("id", id);
  space.Set("title", title);
  space.Set("profile", std::move(profile));
  space.Set("containerIDs", base::ListValue());
  space.Set("newContainerIDs",
            base::ListValue()
                .Append(base::DictValue().Set("pinned", base::DictValue()))
                .Append(pinned_root)
                .Append(base::DictValue().Set(
                    "unpinned",
                    base::DictValue().Set(
                        "_0", base::DictValue().Set("shared",
                                                    base::DictValue()))))
                .Append(unpinned_root));
  spaces->Append(id);
  spaces->Append(std::move(space));
}

std::string MultiProfileSidebar() {
  base::ListValue spaces;
  base::ListValue items;
  AddSpace(&spaces, &items, "space-home", "Home",
           base::DictValue().Set("default", true), {"tab-home"}, {});
  AddSpace(&spaces, &items, "space-work", "Work", CustomProfile("Profile 1"),
           {"folder-w"}, {"split-w"});
  AddSpace(&spaces, &items, "space-solo", "Solo", CustomProfile("Profile 2"),
           {"tab-solo"}, {});
  AddSpace(&spaces, &items, "space-side", "Side", CustomProfile("Profile 1"),
           {"tab-side"}, {});
  AddItem(&items, "tab-home", "space-home-pinned", {}, "Home page",
          Tab("https://home.example.test/"));
  AddItem(&items, "folder-w", "space-work-pinned", {"tab-w1"}, "Docs",
          base::DictValue().Set("list", base::DictValue()));
  AddItem(&items, "tab-w1", "folder-w", {}, "Doc",
          Tab("https://docs.example.test/"));
  AddItem(&items, "split-w", "space-work-unpinned", {"tab-s1", "tab-s2"},
          std::nullopt, Split());
  AddItem(&items, "tab-s1", "split-w", {}, "Top",
          Tab("https://top.example.test/"));
  AddItem(&items, "tab-s2", "split-w", {}, "Bottom",
          Tab("https://bottom.example.test/"));
  AddItem(&items, "tab-solo", "space-solo-pinned", {}, "Solo page",
          Tab("https://solo.example.test/"));
  AddItem(&items, "tab-side", "space-side-pinned", {}, "Side page",
          Tab("https://side.example.test/"));

  base::DictValue root;
  root.Set("version", 1);
  root.Set(
      "sidebar",
      base::DictValue().Set(
          "containers",
          base::ListValue()
              .Append(base::DictValue().Set("global", base::DictValue()))
              .Append(base::DictValue()
                          .Set("spaces", std::move(spaces))
                          .Set("items", std::move(items)))));
  return base::WriteJson(root).value_or(std::string());
}

ArcImportPlan ParseFixture() {
  ArcParseResult parsed = ParseArcSnapshot(SnapshotFor(MultiProfileSidebar()));
  EXPECT_EQ(ArcImportStatus::kOk, parsed.status);
  return parsed.plan.value_or(ArcImportPlan());
}

base::Uuid SpaceWorkspaceId(std::string_view space_id) {
  return MakeDeterministicArcId(internal::kWorkspaceIdDomain, space_id);
}

base::Uuid ItemId(std::string_view item_id) {
  return MakeDeterministicArcId(internal::kItemIdDomain, item_id);
}

const tab_tree::TreeNode* FindNode(const tab_tree::TabTreeSnapshot& tree,
                                   const base::Uuid& id) {
  for (const tab_tree::TreeNode& node : tree.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

TEST(ArcImportProfileMappingTest, PlanRecordsArcProfilesInSidebarOrder) {
  const ArcImportPlan plan = ParseFixture();

  EXPECT_EQ((std::vector<ArcImportProfileSpaces>{
                {.directory_name = "Default", .space_titles = {"Home"}},
                {.directory_name = "Profile 1",
                 .space_titles = {"Work", "Side"}},
                {.directory_name = "Profile 2", .space_titles = {"Solo"}},
            }),
            plan.arc_profiles);
  ASSERT_EQ(4u, plan.workspace_arc_profiles.size());
  EXPECT_EQ("Default",
            plan.workspace_arc_profiles.at(SpaceWorkspaceId("space-home")));
  EXPECT_EQ("Profile 1",
            plan.workspace_arc_profiles.at(SpaceWorkspaceId("space-work")));
  EXPECT_EQ("Profile 1",
            plan.workspace_arc_profiles.at(SpaceWorkspaceId("space-side")));
  EXPECT_EQ("Profile 2",
            plan.workspace_arc_profiles.at(SpaceWorkspaceId("space-solo")));
}

TEST(ArcImportProfileMappingTest, NoSeparationKeepsThePlanUnchanged) {
  const ArcImportPlan plan = ParseFixture();
  const ArcProfileMapping mapping = MapArcImportPlanByProfile(plan, {});

  EXPECT_EQ(plan, mapping.main_plan);
  EXPECT_TRUE(mapping.separated.empty());
}

TEST(ArcImportProfileMappingTest, SeparatedProfileLeavesTheMainPlan) {
  const ArcImportPlan plan = ParseFixture();
  ASSERT_EQ(1u, plan.splits.size());
  const ArcProfileMapping mapping =
      MapArcImportPlanByProfile(plan, {"Profile 1"});

  // The main plan keeps Home and Solo with their original identities.
  ASSERT_EQ(2u, mapping.main_plan.tree.workspaces.size());
  EXPECT_EQ(SpaceWorkspaceId("space-home"),
            mapping.main_plan.tree.workspaces[0].id);
  EXPECT_EQ(SpaceWorkspaceId("space-solo"),
            mapping.main_plan.tree.workspaces[1].id);
  EXPECT_TRUE(mapping.main_plan.splits.empty());
  EXPECT_EQ(2u, mapping.main_plan.tree.nodes.size());
  EXPECT_TRUE(FindNode(mapping.main_plan.tree, ItemId("tab-home")));
  EXPECT_TRUE(FindNode(mapping.main_plan.tree, ItemId("tab-solo")));
  EXPECT_EQ(2u, mapping.main_plan.workspace_arc_profiles.size());

  ASSERT_EQ(1u, mapping.separated.size());
  const ArcSeparatedWorkspacePlan& separated = mapping.separated.front();
  EXPECT_EQ("Profile 1", separated.arc_profile);
  EXPECT_EQ(ArcSeparatedWorkspaceId("Profile 1"), separated.workspace_id);
  EXPECT_EQ(u"Arc – Profile 1", separated.name);
  EXPECT_EQ(2u, separated.space_count);
  EXPECT_EQ(4u, separated.page_count);
  // Two space folders, the Docs folder and the (plain) split folder.
  EXPECT_EQ(4u, separated.folder_count);

  ASSERT_EQ(1u, separated.tree.workspaces.size());
  const tab_tree::Workspace& workspace = separated.tree.workspaces.front();
  EXPECT_EQ(separated.workspace_id, workspace.id);
  EXPECT_EQ(separated.name, workspace.name);
  EXPECT_EQ("0", workspace.sort_key);
  EXPECT_TRUE(workspace.icon.empty());
  EXPECT_FALSE(workspace.accent_argb.has_value());
  EXPECT_EQ(tab_tree::Workspace().archive_policy, workspace.archive_policy);

  // Every source node moved exactly once, plus one folder per space.
  EXPECT_EQ(plan.tree.nodes.size() + 2,
            mapping.main_plan.tree.nodes.size() + separated.tree.nodes.size());
  std::vector<const tab_tree::TreeNode*> roots;
  for (const tab_tree::TreeNode& node : separated.tree.nodes) {
    EXPECT_EQ(separated.workspace_id, node.workspace_id);
    if (!node.parent_id.has_value()) {
      roots.push_back(&node);
    }
  }
  ASSERT_EQ(2u, roots.size());
  EXPECT_EQ(u"Work", roots[0]->title);
  EXPECT_EQ(internal::SortKey(0), roots[0]->sort_key);
  EXPECT_EQ(u"Side", roots[1]->title);
  EXPECT_EQ(internal::SortKey(1), roots[1]->sort_key);
  EXPECT_EQ(MakeDeterministicArcId(
                kArcSeparatedSpaceFolderIdDomain,
                SpaceWorkspaceId("space-work").AsLowercaseString()),
            roots[0]->id);

  const tab_tree::TreeNode* docs = FindNode(separated.tree, ItemId("folder-w"));
  ASSERT_TRUE(docs);
  EXPECT_EQ(roots[0]->id, docs->parent_id);
  const tab_tree::TreeNode* doc = FindNode(separated.tree, ItemId("tab-w1"));
  ASSERT_TRUE(doc);
  EXPECT_EQ(docs->id, doc->parent_id);
  const tab_tree::TreeNode* split = FindNode(separated.tree, ItemId("split-w"));
  ASSERT_TRUE(split);
  EXPECT_EQ(roots[0]->id, split->parent_id);
  EXPECT_EQ(u"folder", split->icon);
  const tab_tree::TreeNode* side =
      FindNode(separated.tree, ItemId("tab-side"));
  ASSERT_TRUE(side);
  EXPECT_EQ(roots[1]->id, side->parent_id);
}

TEST(ArcImportProfileMappingTest, OneSpaceProfileKeepsItsStructureAtRoot) {
  const ArcProfileMapping mapping =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 2"});

  ASSERT_EQ(1u, mapping.separated.size());
  const ArcSeparatedWorkspacePlan& separated = mapping.separated.front();
  EXPECT_EQ(u"Solo", separated.name);
  EXPECT_EQ(1u, separated.space_count);
  ASSERT_EQ(1u, separated.tree.nodes.size());
  const tab_tree::TreeNode& page = separated.tree.nodes.front();
  EXPECT_EQ(ItemId("tab-solo"), page.id);
  EXPECT_FALSE(page.parent_id.has_value());
  EXPECT_EQ(separated.workspace_id, page.workspace_id);
  EXPECT_EQ(3u, mapping.main_plan.tree.workspaces.size());
  // The split stays in the main plan with its Workspace.
  EXPECT_EQ(1u, mapping.main_plan.splits.size());
}

TEST(ArcImportProfileMappingTest, SeparatedIdentitiesAreDeterministic) {
  const ArcProfileMapping first =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 1", "Profile 2"});
  const ArcProfileMapping second =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 2", "Profile 1"});

  // Selection order does not matter; the plan order does.
  ASSERT_EQ(2u, first.separated.size());
  EXPECT_EQ(first.separated, second.separated);
  EXPECT_EQ(first.main_plan, second.main_plan);
  EXPECT_EQ("Profile 1", first.separated[0].arc_profile);
  EXPECT_EQ("Profile 2", first.separated[1].arc_profile);
  EXPECT_TRUE(first.separated[0].workspace_id.is_valid());
  EXPECT_NE(first.separated[0].workspace_id, first.separated[1].workspace_id);
  // Never the identity of a Workspace in the main Profile.
  EXPECT_NE(SpaceWorkspaceId("space-solo"), first.separated[1].workspace_id);
  EXPECT_FALSE(ArcSeparatedWorkspaceId("").is_valid());
}

TEST(ArcImportProfileMappingTest, ValidatesTheSeparatedSelection) {
  const ArcImportPlan plan = ParseFixture();

  EXPECT_TRUE(IsValidArcSeparatedProfiles(plan, {}));
  EXPECT_TRUE(IsValidArcSeparatedProfiles(plan, {"Profile 1"}));
  EXPECT_TRUE(
      IsValidArcSeparatedProfiles(plan, {"Default", "Profile 1", "Profile 2"}));
  EXPECT_FALSE(IsValidArcSeparatedProfiles(plan, {"Profile 9"}));
  EXPECT_FALSE(IsValidArcSeparatedProfiles(plan, {"Profile 1", "Profile 1"}));
  EXPECT_FALSE(IsValidArcSeparatedProfiles(plan, {""}));
}

TEST(ArcImportProfileMappingTest, SeparatedTreeIsAPortableSelection) {
  const ArcProfileMapping mapping =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 1"});
  ASSERT_EQ(1u, mapping.separated.size());
  const ArcSeparatedWorkspacePlan& separated = mapping.separated.front();

  const std::optional<tab_tree::PortableWorkspaceSelection> portable =
      SelectArcSeparatedPortableTree(separated);
  ASSERT_TRUE(portable.has_value());
  ASSERT_EQ(1u, portable->workspaces.size());
  EXPECT_EQ(separated.workspace_id, portable->workspaces.front().id);
  EXPECT_EQ(separated.name, portable->workspaces.front().name);
  EXPECT_EQ("0", portable->workspaces.front().sort_key);
  EXPECT_EQ(separated.tree.nodes.size(), portable->nodes.size());
  EXPECT_EQ(0u, portable->excluded_nonportable_pages);
  EXPECT_EQ(0u, portable->excluded_temporary_pages);

  ArcSeparatedWorkspacePlan broken = separated;
  broken.tree.workspaces.clear();
  EXPECT_FALSE(SelectArcSeparatedPortableTree(broken).has_value());
}

// Everything separated leaves an empty main plan, which the transaction
// treats as a no-op instead of a failure.
TEST(ArcImportProfileMappingTest, FullySeparatedMainPlanIsATransactionNoOp) {
  const ArcProfileMapping mapping = MapArcImportPlanByProfile(
      ParseFixture(), {"Default", "Profile 1", "Profile 2"});

  EXPECT_TRUE(mapping.main_plan.tree.workspaces.empty());
  EXPECT_TRUE(mapping.main_plan.tree.nodes.empty());
  EXPECT_EQ(3u, mapping.separated.size());
  const ArcImportMergeResult merge = MergeArcImportPlan(
      tab_tree::TabTreeSnapshot(), mapping.main_plan,
      ArcConflictResolution::kRename);
  EXPECT_EQ(ArcImportStatus::kNoChanges, merge.status);
}

// A repeated import derives the same Workspace identity, so the registry
// lookup CreateIsolatedWorkspaceWithStructure() performs finds the Profile of
// the first import and a second Profile can never be registered for it.
TEST(ArcImportProfileMappingTest, RepeatedImportFindsTheSeparatedWorkspace) {
  TestingPrefServiceSimple local_state;
  session::RegisterIsolatedProfileLocalState(local_state.registry());
  const ArcProfileMapping first =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 1"});
  ASSERT_EQ(1u, first.separated.size());
  ASSERT_TRUE(session::AddIsolatedProfile(
      &local_state,
      session::IsolatedProfileEntry{
          .profile_dir = "Profile 7",
          .workspace_id = first.separated.front().workspace_id,
          .name = first.separated.front().name,
          .state = session::IsolatedProfileState::kActive,
          .sort_key = "1"}));

  const ArcProfileMapping repeated =
      MapArcImportPlanByProfile(ParseFixture(), {"Profile 1"});
  ASSERT_EQ(1u, repeated.separated.size());
  const std::optional<session::IsolatedProfileEntry> existing =
      session::FindIsolatedProfileByWorkspaceId(
          &local_state, repeated.separated.front().workspace_id);
  ASSERT_TRUE(existing.has_value());
  EXPECT_EQ("Profile 7", existing->profile_dir);
  EXPECT_FALSE(session::AddIsolatedProfile(
      &local_state,
      session::IsolatedProfileEntry{
          .profile_dir = "Profile 8",
          .workspace_id = repeated.separated.front().workspace_id,
          .name = repeated.separated.front().name,
          .sort_key = "2"}));
  EXPECT_EQ(1u, session::GetIsolatedProfiles(&local_state).size());
}

}  // namespace

}  // namespace ahoi::importer::arc
