// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/sync/sync_serialization.h"
#include "ahoi/browser/sync/tab_tree_sync_adapter.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::sync {
namespace {

std::string Text(const base::DictValue& value, const char* key) {
  const auto* text = value.FindString(key);
  EXPECT_TRUE(text) << key;
  return text ? *text : "";
}

base::Uuid Id(const std::string& value) {
  auto id = base::Uuid::ParseLowercase(value);
  EXPECT_TRUE(id.is_valid()) << value;
  return id;
}

std::optional<base::Uuid> OptionalId(const base::DictValue& value,
                                   const char* key) {
  const auto* id = value.FindString(key);
  return id ? std::make_optional(Id(*id)) : std::nullopt;
}

template <typename T>
std::vector<T> Order(std::vector<T> input, int order) {
  if (order == 1) {
    std::ranges::reverse(input);
  } else if (order == 2 && input.size() > 1) {
    std::rotate(input.begin(), input.begin() + 1, input.end());
  }
  return input;
}

template <typename T>
std::vector<std::string> Encode(const std::vector<T>& records) {
  std::vector<std::string> result;
  for (const auto& record : records) {
    std::string bytes;
    EXPECT_TRUE(SerializeRecord(record, &bytes));
    result.push_back(std::move(bytes));
  }
  return result;
}

template <typename T>
bool Decode(const base::ListValue& values,
            EntityType type,
            std::vector<T>* records) {
  for (const auto& value : values) {
    std::string bytes;
    SyncRecord decoded;
    if (!base::JSONWriter::Write(value, &bytes) ||
        !DeserializeRecord(type, bytes, &decoded)) {
      return false;
    }
    const auto* record = std::get_if<T>(&decoded);
    if (!record) {
      return false;
    }
    records->push_back(*record);
  }
  return true;
}

void CheckFrames(const char* fixture_name) {
  base::FilePath source_root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(
      source_root.AppendASCII(
          "ahoi/browser/sync/testdata").AppendASCII(fixture_name),
      &bytes));
  auto document = base::JSONReader::Read(bytes, base::JSON_PARSE_RFC);
  ASSERT_TRUE(document && document->is_dict());
  const auto& root = document->GetDict();
  EXPECT_EQ(root.FindInt("schemaVersion"), 1);
  EXPECT_EQ(root.FindInt("modelVersion"), 3);
  const auto* cases = root.FindList("cases");
  ASSERT_TRUE(cases);
  size_t frames = 0;
  size_t executed = 0;
  for (const auto& case_value : *cases) {
    const auto& test_case = case_value.GetDict();
    const auto* case_frames = test_case.FindList("frames");
    ASSERT_TRUE(case_frames);
    std::map<base::Uuid, std::string> previous_nodes;
    for (const auto& frame_value : *case_frames) {
      const auto& frame = frame_value.GetDict();
      SCOPED_TRACE(Text(test_case, "name") + "/" + Text(frame, "name"));
      const auto* input_workspaces = frame.FindList("workspaces");
      const auto* input_nodes = frame.FindList("nodes");
      const auto* expect = frame.FindDict("expect");
      ASSERT_TRUE(input_workspaces && input_nodes && expect);
      std::vector<WorkspaceRecord> workspaces;
      std::vector<TreeNodeRecord> nodes;
      ASSERT_TRUE(Decode(*input_workspaces, EntityType::kWorkspace, &workspaces));
      ASSERT_TRUE(Decode(*input_nodes, EntityType::kTreeNode, &nodes));
      const auto encoded_nodes = Encode(nodes);
      std::map<base::Uuid, std::string> node_bytes;
      for (size_t index = 0; index < nodes.size(); ++index) {
        node_bytes.emplace(nodes[index].id, encoded_nodes[index]);
      }
      const auto* unchanged = frame.FindList("unchangedNodeIdsFromPreviousFrame");
      ASSERT_TRUE(unchanged);
      for (const auto& id : *unchanged) {
        const auto key = Id(id.GetString());
        ASSERT_TRUE(previous_nodes.contains(key) && node_bytes.contains(key));
        EXPECT_EQ(previous_nodes.at(key), node_bytes.at(key));
      }
      const auto* routes = expect->FindList("workspaceRoutes");
      const auto* effective = expect->FindList("effectiveNodes");
      const auto* sibling_groups = expect->FindList("siblingOrder");
      const auto* not_live = expect->FindList("notLiveNodeIds");
      ASSERT_TRUE(routes && effective && sibling_groups && not_live);
      for (int workspace_order = 0; workspace_order < 3; ++workspace_order) {
        for (int node_order = 0; node_order < 3; ++node_order) {
          SCOPED_TRACE(testing::Message() << "workspace order " << workspace_order
                                         << ", node order " << node_order);
          const auto ws = Order(workspaces, workspace_order);
          const auto pages = Order(nodes, node_order);
          const auto before_ws = Encode(ws);
          const auto before_pages = Encode(pages);
          const auto before_ws_records = ws;
          const auto before_page_records = pages;
          for (const auto& route_value : *routes) {
            const auto& route = route_value.GetDict();
            SCOPED_TRACE(Text(route, "classification"));
            EXPECT_EQ(ResolveWorkspaceMergeTarget(Id(Text(route, "workspaceId")),
                                                  ws),
                      OptionalId(route, "targetWorkspaceId"));
          }
          const auto applied = ReconcileTabTreeRecords({}, ws, pages);
          const auto repeated = ReconcileTabTreeRecords({}, ws, pages);
          EXPECT_EQ(applied, repeated);
          EXPECT_EQ(Encode(ws), before_ws);
          EXPECT_EQ(Encode(pages), before_pages);
          EXPECT_EQ(ws, before_ws_records);  // Includes each field clock.
          EXPECT_EQ(pages, before_page_records);
          if (!applied) {
            EXPECT_TRUE(effective->empty());  // Unresolved, no live fallback.
          } else {
            for (const auto& expected_value : *effective) {
              const auto& expected = expected_value.GetDict();
              const auto id = Id(Text(expected, "id"));
              const auto node = std::ranges::find(
                  applied->nodes, id, &tab_tree::TreeNode::id);
              ASSERT_NE(node, applied->nodes.end());
              EXPECT_FALSE(node->tombstone);
              EXPECT_EQ(node->workspace_id, Id(Text(expected, "workspaceId")));
              EXPECT_EQ(node->parent_id, OptionalId(expected, "parentId"));
            }
            for (const auto& group_value : *sibling_groups) {
              const auto& group = group_value.GetDict();
              const auto* ids = group.FindList("nodeIds");
              ASSERT_TRUE(ids);
              std::set<base::Uuid> constrained;
              std::vector<base::Uuid> expected;
              for (const auto& value : *ids) {
                expected.push_back(Id(value.GetString()));
                constrained.insert(expected.back());
              }
              std::vector<const tab_tree::TreeNode*> siblings;
              for (const auto& node : applied->nodes) {
                if (!node.tombstone && constrained.contains(node.id) &&
                    node.workspace_id == Id(Text(group, "workspaceId")) &&
                    node.parent_id == OptionalId(group, "parentId")) {
                  siblings.push_back(&node);
                }
              }
              std::ranges::sort(siblings, [](const auto* a, const auto* b) {
                return std::tie(a->sort_key, a->id) <
                       std::tie(b->sort_key, b->id);
              });
              std::vector<base::Uuid> actual;
              for (const auto* node : siblings) {
                actual.push_back(node->id);
              }
              EXPECT_EQ(actual, expected);
            }
            for (const auto& value : *not_live) {
              const auto node = std::ranges::find(
                  applied->nodes, Id(value.GetString()), &tab_tree::TreeNode::id);
              EXPECT_TRUE(node == applied->nodes.end() || node->tombstone);
            }
          }
          ++executed;
        }
      }
      previous_nodes = std::move(node_bytes);
      ++frames;
    }
  }
  EXPECT_GT(executed, 0u);
  EXPECT_EQ(executed, frames * 9);
}

TEST(SyncWorkspaceProjectionConformanceTest, SharedFramesAndArrayOrders) {
  CheckFrames("workspace_merge_projection_v3.json");
}

TEST(SyncWorkspaceProjectionConformanceTest, MarkerCollisionFrames) {
  CheckFrames("workspace_merge_marker_collision_v3.json");
}

}  // namespace
}  // namespace ahoi::sync
