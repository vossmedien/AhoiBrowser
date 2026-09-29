// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tests that the Arc parser reads the rendered sidebar (`sidebar.containers`)
// and keeps a deep tree intact. The synthetic fixture mirrors the shape of a
// real Arc sidebar: top-level folders in the pinned container, folders nested
// up to depth 8, a split inside a deep folder, a standalone (Little Arc)
// container, and a stale Arc Sync mirror that must never be imported.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_source_model.h"
#include "ahoi/browser/importer/arc/arc_import_unittest_support.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

using test_support::SnapshotFor;

struct Item {
  std::string_view id;
  std::optional<std::string_view> parent;
  std::vector<std::string_view> children;
  std::string_view title;
  enum { kContainer, kList, kSplit, kTab } kind;
};

// Pinned: [tab-loose, Agency, Private]; unpinned: [tab-temp].
// Agency > Projects > Client > Phase > Sprint > Deep holds a split (depth 7,
// members at depth 8) and Archive (depth 7) with a page at depth 8.
std::vector<Item> DeepItems() {
  return {
      {"root-pinned", std::nullopt, {"tab-loose", "agency", "private"}, "",
       Item::kContainer},
      {"root-unpinned", std::nullopt, {"tab-temp"}, "", Item::kContainer},
      {"tab-loose", "root-pinned", {}, "Loose", Item::kTab},
      {"tab-temp", "root-unpinned", {}, "Temp", Item::kTab},
      {"agency", "root-pinned", {"tab-agency", "projects"}, "Agency",
       Item::kList},
      {"tab-agency", "agency", {}, "Agency page", Item::kTab},
      {"projects", "agency", {"client", "tab-projects"}, "Projects",
       Item::kList},
      {"client", "projects", {"phase"}, "Client", Item::kList},
      {"tab-projects", "projects", {}, "Projects page", Item::kTab},
      {"phase", "client", {"sprint"}, "Phase", Item::kList},
      {"sprint", "phase", {"deep"}, "Sprint", Item::kList},
      {"deep", "sprint", {"split", "archive"}, "Deep", Item::kList},
      {"split", "deep", {"split-left", "split-right"}, "", Item::kSplit},
      {"split-left", "split", {}, "Left", Item::kTab},
      {"split-right", "split", {}, "Right", Item::kTab},
      {"archive", "deep", {"tab-archive"}, "Archive", Item::kList},
      {"tab-archive", "archive", {}, "Archived page", Item::kTab},
      {"private", "root-pinned", {"nested"}, "Private", Item::kList},
      {"nested", "private", {"tab-nested"}, "Nested", Item::kList},
      {"tab-nested", "nested", {}, "Nested page", Item::kTab},
  };
}

base::ListValue Ids(const std::vector<std::string_view>& ids) {
  base::ListValue list;
  for (std::string_view id : ids) {
    list.Append(id);
  }
  return list;
}

base::DictValue Data(const Item& item) {
  if (item.kind == Item::kContainer) {
    return base::DictValue().Set(
        "itemContainer",
        base::DictValue().Set(
            "containerType",
            base::DictValue().Set(
                "spaceItems", base::DictValue().Set("_0", "space-crew"))));
  }
  if (item.kind == Item::kList) {
    return base::DictValue().Set("list", base::DictValue());
  }
  if (item.kind == Item::kSplit) {
    return base::DictValue().Set(
        "splitView", base::DictValue()
                         .Set("layoutOrientation", "vertical")
                         .Set("focusItemID", "split-left"));
  }
  return base::DictValue().Set(
      "tab", base::DictValue().Set(
                 "savedURL",
                 base::StrCat({"https://", item.id, ".example.test/"})));
}

base::ListValue SerializedItems(const std::vector<Item>& items) {
  base::ListValue list;
  for (const Item& item : items) {
    list.Append(item.id);
    list.Append(base::DictValue()
                    .Set("id", item.id)
                    .Set("parentID", item.parent ? base::Value(*item.parent)
                                                 : base::Value())
                    .Set("childrenIds", Ids(item.children))
                    .Set("title", item.title.empty() ? base::Value()
                                                     : base::Value(item.title))
                    .Set("data", Data(item)));
  }
  return list;
}

base::DictValue Space() {
  return base::DictValue()
      .Set("id", "space-crew")
      .Set("title", "Crew")
      .Set("containerIDs", base::ListValue())
      .Set("newContainerIDs",
           base::ListValue()
               .Append(base::DictValue().Set("pinned", base::DictValue()))
               .Append("root-pinned")
               .Append(base::DictValue().Set(
                   "unpinned",
                   base::DictValue().Set(
                       "_0",
                       base::DictValue().Set("shared", base::DictValue()))))
               .Append("root-unpinned"));
}

// The shape Arc Sync leaves behind: a stale subset in which a deep folder
// hangs directly in the pinned container.
base::DictValue StaleSyncMirror() {
  const std::vector<Item> stale = {
      {"root-pinned", std::nullopt, {"client"}, "", Item::kContainer},
      {"root-unpinned", std::nullopt, {}, "", Item::kContainer},
      {"client", "root-pinned", {}, "Client", Item::kList},
  };
  base::ListValue items;
  for (base::Value& entry : SerializedItems(stale)) {
    items.Append(entry.is_dict()
                     ? base::Value(base::DictValue().Set("value",
                                                         std::move(entry)))
                     : std::move(entry));
  }
  return base::DictValue()
      .Set("container",
           base::DictValue().Set(
               "value", base::DictValue().Set("orderedSpaceIDs",
                                              Ids({"space-crew"}))))
      .Set("spaceModels",
           base::ListValue().Append("space-crew").Append(
               base::DictValue().Set("value", Space())))
      .Set("items", std::move(items));
}

struct Options {
  bool with_sync_mirror = false;
  bool with_standalone = false;
};

base::ListValue Containers(const Options& options) {
  base::ListValue containers;
  containers.Append(base::DictValue().Set("global", base::DictValue()));
  containers.Append(
      base::DictValue()
          .Set("spaces", base::ListValue().Append("space-crew").Append(Space()))
          .Set("items", SerializedItems(DeepItems())));
  if (options.with_standalone) {
    containers.Append(base::DictValue().Set(
        "standalone", base::DictValue().Set("_0", "window-a")));
    containers.Append(base::DictValue().Set(
        "items", base::ListValue()
                     .Append("little-a")
                     .Append(base::DictValue())
                     .Append("little-b")
                     .Append(base::DictValue())));
  }
  return containers;
}

std::string Sidebar(const Options& options,
                    std::optional<base::ListValue> containers = std::nullopt) {
  base::DictValue root;
  root.Set("version", 1);
  root.Set("sidebar",
           base::DictValue().Set(
               "containers", containers ? std::move(*containers)
                                        : Containers(options)));
  if (options.with_sync_mirror) {
    root.Set("sidebarSyncState", StaleSyncMirror());
  }
  return base::WriteJson(root).value_or(std::string());
}

ArcParseResult Parse(const std::string& json,
                     const ArcImportPlanOptions& options = {}) {
  return ParseArcSnapshot(SnapshotFor(json), options);
}

const tab_tree::TreeNode* Node(const ArcImportPlan& plan,
                               std::string_view domain,
                               std::string_view source_id) {
  const base::Uuid id = MakeDeterministicArcId(domain, source_id);
  for (const tab_tree::TreeNode& node : plan.tree.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

TEST(ArcImportSourceTreeTest, KeepsEveryItemOfADeepTree) {
  const ArcParseResult parsed = Parse(Sidebar({}));
  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  const ArcImportPlan& plan = *parsed.plan;

  EXPECT_EQ(DeepItems().size(), plan.stats.source_item_count);
  EXPECT_EQ(1u, plan.stats.imported_workspace_count);
  // Nine lists plus the split folder; containers are not nodes.
  EXPECT_EQ(10u, plan.stats.imported_folder_count);
  EXPECT_EQ(8u, plan.stats.imported_page_count);
  EXPECT_EQ(0u, plan.stats.degraded_split_count);
  EXPECT_EQ(1u, plan.stats.imported_split_count);
  EXPECT_EQ(2u, plan.stats.source_top_level_folder_count);
  EXPECT_EQ(0u, plan.stats.skipped_unsupported_item_count);
  EXPECT_EQ(0u, plan.stats.ignored_unreachable_item_count);
  EXPECT_EQ(18u, plan.tree.nodes.size());

  // Every non-container item hangs under its own Arc parent, at its own
  // source position; nothing is flattened or reparented.
  std::map<std::string_view, size_t> positions;
  for (const Item& item : DeepItems()) {
    for (size_t index = 0; index < item.children.size(); ++index) {
      positions[item.children[index]] = index;
    }
  }
  size_t top_level_position = 0;
  for (const Item& item : DeepItems()) {
    if (item.kind == Item::kContainer) {
      continue;
    }
    SCOPED_TRACE(item.id);
    const tab_tree::TreeNode* node = Node(plan, internal::kItemIdDomain,
                                          item.id);
    ASSERT_TRUE(node);
    const bool top_level = item.parent->starts_with("root-");
    if (top_level) {
      EXPECT_FALSE(node->parent_id.has_value());
      ++top_level_position;
    } else {
      EXPECT_EQ(MakeDeterministicArcId(internal::kItemIdDomain, *item.parent),
                node->parent_id);
      EXPECT_EQ(internal::SortKey(positions[item.id]), node->sort_key);
    }
  }
  EXPECT_EQ(4u, top_level_position);
  ASSERT_EQ(1u, plan.splits.size());
  EXPECT_EQ(MakeDeterministicArcId(internal::kItemIdDomain, "split"),
            plan.splits.front().folder_node_id);
}

TEST(ArcImportSourceTreeTest, PromotesOnlyDirectPinnedFolders) {
  const ArcParseResult parsed =
      Parse(Sidebar({}), {.folders_as_workspaces = true});
  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  const ArcImportPlan& plan = *parsed.plan;

  std::vector<std::u16string> names;
  for (const tab_tree::Workspace& workspace : plan.tree.workspaces) {
    names.push_back(workspace.name);
  }
  EXPECT_EQ(std::vector<std::u16string>({u"Crew", u"Agency", u"Private"}),
            names);
  EXPECT_EQ(2u, plan.stats.folder_workspace_count);
  EXPECT_EQ(2u, plan.stats.source_top_level_folder_count);
  EXPECT_EQ(8u, plan.stats.imported_folder_count);
  EXPECT_EQ(8u, plan.stats.imported_page_count);
  EXPECT_EQ(1u, plan.stats.imported_split_count);

  const std::string_view domain = internal::kFolderLayoutItemIdDomain;
  const tab_tree::TreeNode* projects = Node(plan, domain, "projects");
  const tab_tree::TreeNode* client = Node(plan, domain, "client");
  const tab_tree::TreeNode* left = Node(plan, domain, "split-left");
  ASSERT_TRUE(projects && client && left);
  EXPECT_FALSE(projects->parent_id.has_value());
  EXPECT_EQ(MakeDeterministicArcId(internal::kFolderWorkspaceIdDomain,
                                   "agency"),
            projects->workspace_id);
  EXPECT_EQ(projects->id, client->parent_id);
  EXPECT_EQ(MakeDeterministicArcId(domain, "split"), left->parent_id);
  EXPECT_EQ(projects->workspace_id, left->workspace_id);
}

TEST(ArcImportSourceTreeTest, IgnoresTheArcSyncMirror) {
  const ArcParseResult rendered = Parse(Sidebar({}));
  const ArcParseResult mirrored = Parse(Sidebar({.with_sync_mirror = true}));
  ASSERT_EQ(ArcImportStatus::kOk, rendered.status);
  ASSERT_EQ(ArcImportStatus::kOk, mirrored.status);
  EXPECT_EQ(*rendered.plan, *mirrored.plan);

  base::DictValue only_mirror;
  only_mirror.Set("version", 1);
  only_mirror.Set("sidebarSyncState", StaleSyncMirror());
  EXPECT_EQ(ArcImportStatus::kMissingRequiredField,
            Parse(base::WriteJson(only_mirror).value_or("")).status);
}

TEST(ArcImportSourceTreeTest, CountsStandaloneItemsAsUnreachable) {
  const ArcParseResult parsed = Parse(Sidebar({.with_standalone = true}));
  ASSERT_EQ(ArcImportStatus::kOk, parsed.status);
  EXPECT_EQ(DeepItems().size() + 2, parsed.plan->stats.source_item_count);
  EXPECT_EQ(2u, parsed.plan->stats.ignored_unreachable_item_count);
  EXPECT_EQ(8u, parsed.plan->stats.imported_page_count);
}

TEST(ArcImportSourceTreeTest, RejectsUnexpectedContainerMaps) {
  base::ListValue no_global;
  no_global.Append(
      base::DictValue().Set("standalone", base::DictValue().Set("_0", "w")));
  no_global.Append(base::DictValue().Set("items", base::ListValue()));
  EXPECT_EQ(ArcImportStatus::kMissingRequiredField,
            Parse(Sidebar({}, std::move(no_global))).status);

  base::ListValue unknown = Containers({});
  unknown.Append(base::DictValue().Set("floating", base::DictValue()));
  unknown.Append(base::DictValue().Set("items", base::ListValue()));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            Parse(Sidebar({}, std::move(unknown))).status);

  base::ListValue two_globals = Containers({});
  base::ListValue second = Containers({});
  for (base::Value& entry : second) {
    two_globals.Append(std::move(entry));
  }
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            Parse(Sidebar({}, std::move(two_globals))).status);

  base::ListValue dangling = Containers({});
  dangling.Append(base::DictValue().Set("global", base::DictValue()));
  EXPECT_EQ(ArcImportStatus::kMalformedSerializedMap,
            Parse(Sidebar({}, std::move(dangling))).status);
}

}  // namespace

}  // namespace ahoi::importer::arc
