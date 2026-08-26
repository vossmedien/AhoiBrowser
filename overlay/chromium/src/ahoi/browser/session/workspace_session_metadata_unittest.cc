// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/workspace_session_metadata.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {

namespace {

base::Uuid Uuid(std::string_view value) {
  base::Uuid uuid = base::Uuid::ParseLowercase(value);
  EXPECT_TRUE(uuid.is_valid());
  return uuid;
}

constexpr char kWorkspaceOne[] = "10000000-0000-4000-8000-000000000001";
constexpr char kWorkspaceTwo[] = "10000000-0000-4000-8000-000000000002";
constexpr char kTreeNode[] = "20000000-0000-4000-8000-000000000001";

TEST(WorkspaceSessionMetadataTest, WindowRoundTrips) {
  EXPECT_FALSE(std::string_view(kWindowSessionMetadataExtraDataKey).empty());
  EXPECT_FALSE(std::string_view(kTabSessionMetadataExtraDataKey).empty());
  const WindowSessionMetadata expected{.active_workspace_id =
                                           Uuid(kWorkspaceOne)};
  const std::optional<std::string> encoded =
      EncodeWindowSessionMetadata(expected);
  ASSERT_TRUE(encoded.has_value());

  WindowSessionMetadata decoded;
  EXPECT_EQ(SessionMetadataDecodeResult::kSuccess,
            DecodeWindowSessionMetadata(*encoded, &decoded));
  EXPECT_EQ(expected, decoded);
}

TEST(WorkspaceSessionMetadataTest, TabRoundTripsWithOptionalTreeNode) {
  const TabSessionMetadata expected{
      .workspace_id = Uuid(kWorkspaceTwo),
      .tree_node_id = Uuid(kTreeNode),
      .last_active_in_workspace = true,
  };
  const std::optional<std::string> encoded = EncodeTabSessionMetadata(expected);
  ASSERT_TRUE(encoded.has_value());

  TabSessionMetadata decoded;
  EXPECT_EQ(SessionMetadataDecodeResult::kSuccess,
            DecodeTabSessionMetadata(*encoded, &decoded));
  EXPECT_EQ(expected, decoded);
}

TEST(WorkspaceSessionMetadataTest, TemporaryTabRoundTripsWithoutTreeNode) {
  const TabSessionMetadata expected{
      .workspace_id = Uuid(kWorkspaceOne),
      .tree_node_id = std::nullopt,
      .last_active_in_workspace = false,
  };
  const std::optional<std::string> encoded = EncodeTabSessionMetadata(expected);
  ASSERT_TRUE(encoded.has_value());

  TabSessionMetadata decoded;
  EXPECT_EQ(SessionMetadataDecodeResult::kSuccess,
            DecodeTabSessionMetadata(*encoded, &decoded));
  EXPECT_EQ(expected, decoded);
}

TEST(WorkspaceSessionMetadataTest, RejectsInvalidUuidWithoutPartialMutation) {
  WindowSessionMetadata window{.active_workspace_id = Uuid(kWorkspaceTwo)};
  EXPECT_EQ(
      SessionMetadataDecodeResult::kMalformed,
      DecodeWindowSessionMetadata(
          R"({"version":1,"active_workspace_id":"not-a-uuid"})", &window));
  EXPECT_EQ(Uuid(kWorkspaceTwo), window.active_workspace_id);

  TabSessionMetadata tab{
      .workspace_id = Uuid(kWorkspaceTwo),
      .tree_node_id = Uuid(kTreeNode),
      .last_active_in_workspace = true,
  };
  EXPECT_EQ(
      SessionMetadataDecodeResult::kMalformed,
      DecodeTabSessionMetadata(
          R"({"version":1,"workspace_id":"not-a-uuid","last_active_in_workspace":false})",
          &tab));
  EXPECT_EQ(Uuid(kWorkspaceTwo), tab.workspace_id);
  ASSERT_TRUE(tab.tree_node_id.has_value());
  EXPECT_EQ(Uuid(kTreeNode), *tab.tree_node_id);
  EXPECT_TRUE(tab.last_active_in_workspace);
}

TEST(WorkspaceSessionMetadataTest, RejectsUnsupportedOrExtendedSchema) {
  WindowSessionMetadata metadata;
  EXPECT_EQ(
      SessionMetadataDecodeResult::kUnsupportedVersion,
      DecodeWindowSessionMetadata(
          R"({"version":2,"active_workspace_id":"10000000-0000-4000-8000-000000000001"})",
          &metadata));
  EXPECT_EQ(
      SessionMetadataDecodeResult::kMalformed,
      DecodeWindowSessionMetadata("{\"version\":1,"
                                  "\"active_workspace_id\":"
                                  "\"10000000-0000-4000-8000-000000000001\","
                                  "\"unexpected\":true}",
                                  &metadata));
}

TEST(WorkspaceSessionMetadataTest, InvalidModelCannotBeEncoded) {
  EXPECT_FALSE(
      EncodeWindowSessionMetadata(WindowSessionMetadata{}).has_value());
  EXPECT_FALSE(EncodeTabSessionMetadata(TabSessionMetadata{}).has_value());
  EXPECT_FALSE(EncodeTabSessionMetadata(TabSessionMetadata{
                                            .workspace_id = Uuid(kWorkspaceOne),
                                            .tree_node_id = base::Uuid(),
                                        })
                   .has_value());
}

TEST(WorkspaceSessionMetadataTest, ResolveUsesRequestedWorkspaceOrFallback) {
  const base::Uuid first = Uuid(kWorkspaceOne);
  const base::Uuid second = Uuid(kWorkspaceTwo);
  const std::array<base::Uuid, 3> available = {base::Uuid(), first, second};

  EXPECT_EQ(second, ResolveWorkspaceForRestore(second, base::span(available)));
  EXPECT_EQ(first, ResolveWorkspaceForRestore(
                       Uuid("10000000-0000-4000-8000-000000000099"),
                       base::span(available)));
  EXPECT_EQ(first,
            ResolveWorkspaceForRestore(std::nullopt, base::span(available)));

  const std::array<base::Uuid, 1> invalid = {base::Uuid()};
  EXPECT_FALSE(ResolveWorkspaceForRestore(std::nullopt, base::span(invalid))
                   .has_value());
}

}  // namespace

}  // namespace ahoi::session
