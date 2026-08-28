// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <string_view>

#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kValidArcSidebar[] = R"json({
  "version": 1,
  "sidebarSyncState": {
    "container": {
      "value": {
        "version": 6,
        "orderedSpaceIDs": ["space-a"]
      }
    },
    "spaceModels": [
      "space-a",
      {
        "value": {
          "id": "space-a",
          "title": "Work",
          "containerIDs": [],
          "newContainerIDs": [
            "root-pinned",
            "thebrowser.company.defaultPersonalSpacePinnedContainerID",
            "root-unpinned",
            "thebrowser.company.defaultPersonalSpaceUnpinnedContainerID"
          ]
        }
      }
    ],
    "items": [
      "root-pinned",
      {
        "value": {
          "id": "root-pinned",
          "parentID": null,
          "childrenIds": ["tab-a"],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"spaceItems": {"_0": "space-a"}}
            }
          }
        }
      },
      "root-unpinned",
      {
        "value": {
          "id": "root-unpinned",
          "parentID": null,
          "childrenIds": [
            "folder-a", "split-a", "unsafe-file", "unsafe-creds",
            "unsupported-a"
          ],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"spaceItems": {"_0": "space-a"}}
            }
          }
        }
      },
      "topapps-root",
      {
        "value": {
          "id": "topapps-root",
          "parentID": null,
          "childrenIds": [],
          "title": null,
          "data": {
            "itemContainer": {
              "containerType": {"topApps": {"_0": {}}}
            }
          }
        }
      },
      "tab-a",
      {
        "value": {
          "id": "tab-a",
          "parentID": "root-pinned",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Pinned page",
              "savedURL": "https://pinned.example.test/path"
            }
          }
        }
      },
      "folder-a",
      {
        "value": {
          "id": "folder-a",
          "parentID": "root-unpinned",
          "childrenIds": ["tab-b"],
          "title": "Folder",
          "data": {"list": {}}
        }
      },
      "tab-b",
      {
        "value": {
          "id": "tab-b",
          "parentID": "folder-a",
          "childrenIds": [],
          "title": "Nested page",
          "data": {
            "tab": {
              "savedTitle": "Nested page fallback",
              "savedURL": "https://nested.example.test/"
            }
          }
        }
      },
      "split-a",
      {
        "value": {
          "id": "split-a",
          "parentID": "root-unpinned",
          "childrenIds": ["split-tab-a", "split-tab-b"],
          "title": null,
          "data": {
            "splitView": {
              "layoutOrientation": "horizontal",
              "focusItemID": "split-tab-b",
              "itemWidthFactors": [
                "split-tab-a", 0.5,
                "split-tab-b", 0.5
              ],
              "customInfo": null,
              "timeLastActiveAt": null
            }
          }
        }
      },
      "split-tab-a",
      {
        "value": {
          "id": "split-tab-a",
          "parentID": "split-a",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Left",
              "savedURL": "https://left.example.test/"
            }
          }
        }
      },
      "split-tab-b",
      {
        "value": {
          "id": "split-tab-b",
          "parentID": "split-a",
          "childrenIds": [],
          "title": null,
          "data": {
            "tab": {
              "savedTitle": "Right",
              "savedURL": "https://right.example.test/"
            }
          }
        }
      },
      "unsafe-file",
      {
        "value": {
          "id": "unsafe-file",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Local file",
          "data": {
            "tab": {
              "savedTitle": "Local file",
              "savedURL": "file:///private/example.txt"
            }
          }
        }
      },
      "unsafe-creds",
      {
        "value": {
          "id": "unsafe-creds",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Credential URL",
          "data": {
            "tab": {
              "savedTitle": "Credential URL",
              "savedURL": "https://user:password@example.test/"
            }
          }
        }
      },
      "unsupported-a",
      {
        "value": {
          "id": "unsupported-a",
          "parentID": "root-unpinned",
          "childrenIds": [],
          "title": "Unsupported",
          "data": {"easel": {}}
        }
      }
    ]
  }
})json";

ArcImportSnapshot SnapshotFor(std::string json) {
  ArcImportSnapshot snapshot;
  snapshot.source_size = static_cast<int64_t>(json.size());
  snapshot.sha256 = crypto::hash::Sha256(json);
  snapshot.json = std::move(json);
  return snapshot;
}

bool ReplaceOnce(std::string* value,
                 std::string_view needle,
                 std::string_view replacement) {
  const size_t offset = value->find(needle);
  if (offset == std::string::npos) {
    return false;
  }
  value->replace(offset, needle.size(), replacement);
  return true;
}

class ArcImportFileTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    application_support_ =
        temp_dir_.GetPath().Append(FILE_PATH_LITERAL("Application Support"));
    ASSERT_TRUE(base::CreateDirectory(application_support_));
  }

  base::FilePath ArcRoot() const {
    return application_support_.Append(FILE_PATH_LITERAL("Arc"));
  }

  base::FilePath SidebarFile() const {
    return ArcRoot().Append(FILE_PATH_LITERAL("StorableSidebar.json"));
  }

  base::FilePath BrowserProfile() const {
    return ArcRoot()
        .Append(FILE_PATH_LITERAL("User Data"))
        .Append(FILE_PATH_LITERAL("Default"));
  }

  void CreateArcRootAndProfile() {
    ASSERT_TRUE(base::CreateDirectory(BrowserProfile()));
    ASSERT_TRUE(
        base::WriteFile(BrowserProfile().AppendASCII("Preferences"), "{}"));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath application_support_;
};

TEST_F(ArcImportFileTest, DiscoversAndCapturesStableBoundedSource) {
  CreateArcRootAndProfile();
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));

  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());
  EXPECT_EQ(SidebarFile(), discovery.source->sidebar_file);
  ASSERT_EQ(1u, discovery.source->browser_profiles.size());
  EXPECT_EQ("Default",
            discovery.source->browser_profiles.front().directory_name);
  EXPECT_EQ(BrowserProfile(), discovery.source->browser_profiles.front().path);

  ArcSnapshotResult captured = CaptureArcSnapshot(*discovery.source);
  ASSERT_EQ(ArcImportStatus::kOk, captured.status);
  ASSERT_TRUE(captured.snapshot.has_value());
  EXPECT_EQ(kValidArcSidebar, captured.snapshot->json);
  EXPECT_EQ(crypto::hash::Sha256(kValidArcSidebar), captured.snapshot->sha256);
}

TEST_F(ArcImportFileTest, DiscoversEverySelectableChromiumProfileOnly) {
  CreateArcRootAndProfile();
  const base::FilePath user_data = BrowserProfile().DirName();
  const base::FilePath profile_two = user_data.AppendASCII("Profile 2");
  const base::FilePath guest = user_data.AppendASCII("Guest Profile");
  const base::FilePath system = user_data.AppendASCII("System Profile");
  ASSERT_TRUE(base::CreateDirectory(profile_two));
  ASSERT_TRUE(base::CreateDirectory(guest));
  ASSERT_TRUE(base::CreateDirectory(system));
  ASSERT_TRUE(base::WriteFile(profile_two.AppendASCII("Preferences"), "{}"));
  ASSERT_TRUE(base::WriteFile(guest.AppendASCII("Preferences"), "{}"));
  ASSERT_TRUE(base::WriteFile(system.AppendASCII("Preferences"), "{}"));
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));

  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);

  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());
  ASSERT_EQ(2u, discovery.source->browser_profiles.size());
  EXPECT_EQ("Default", discovery.source->browser_profiles[0].directory_name);
  EXPECT_EQ("Profile 2", discovery.source->browser_profiles[1].directory_name);
}

TEST(ArcImportDiscoveryTest, RecognizesMainAndHelperExecutablesInArcBundle) {
  EXPECT_TRUE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("/Applications/Arc.app/Contents/MacOS/Arc"))));
  EXPECT_TRUE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("/Applications/Arc.app/Contents/Frameworks/"
                        "Arc Helper.app/Contents/MacOS/Arc Helper"))));
  EXPECT_TRUE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("/Applications/Arc.app/Contents/Frameworks/"
                        "Arc Helper (Renderer).app/Contents/MacOS/"
                        "Arc Helper (Renderer)"))));
  EXPECT_FALSE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("/Applications/Arc Helper.app/Contents/MacOS/"
                        "Arc Helper"))));
  EXPECT_FALSE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("/Applications/Arc.app.backup/Contents/MacOS/Arc"))));
  EXPECT_FALSE(internal::IsArcBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("relative/Arc.app/Contents/MacOS/Arc"))));
}

TEST_F(ArcImportFileTest, ProtectsSidebarAndSelectedProfileDatabaseFiles) {
  CreateArcRootAndProfile();
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());
  const ArcSource& source = *discovery.source;

  EXPECT_TRUE(
      internal::IsRelevantArcSourcePath(source, source.sidebar_file));
  for (std::string_view filename : {"Preferences", "Bookmarks", "History",
                                    "History-wal", "History-shm", "Favicons",
                                    "Favicons-wal", "Favicons-shm", "Web Data",
                                    "Web Data-wal", "Web Data-shm"}) {
    EXPECT_TRUE(internal::IsRelevantArcSourcePath(
        source, BrowserProfile().AppendASCII(filename)))
        << filename;
  }

  EXPECT_FALSE(internal::IsRelevantArcSourcePath(
      source, BrowserProfile().AppendASCII("Cache/Cache_Data/index")));
  EXPECT_FALSE(internal::IsRelevantArcSourcePath(
      source, BrowserProfile().AppendASCII("History-journal")));
  EXPECT_FALSE(internal::IsRelevantArcSourcePath(
      source, BrowserProfile()
                  .DirName()
                  .AppendASCII("Profile 2")
                  .AppendASCII("History")));
}

TEST_F(ArcImportFileTest, RejectsSymlinkedArcRoot) {
  const base::FilePath actual_root =
      temp_dir_.GetPath().Append(FILE_PATH_LITERAL("ActualArc"));
  ASSERT_TRUE(base::CreateDirectory(actual_root));
  ASSERT_TRUE(
      base::CreateDirectory(actual_root.Append(FILE_PATH_LITERAL("User Data"))
                                .Append(FILE_PATH_LITERAL("Default"))));
  ASSERT_TRUE(base::WriteFile(actual_root.Append(FILE_PATH_LITERAL("User Data"))
                                  .Append(FILE_PATH_LITERAL("Default"))
                                  .AppendASCII("Preferences"),
                              "{}"));
  ASSERT_TRUE(base::WriteFile(
      actual_root.Append(FILE_PATH_LITERAL("StorableSidebar.json")),
      kValidArcSidebar));
  ASSERT_TRUE(base::CreateSymbolicLink(actual_root, ArcRoot()));

  EXPECT_EQ(ArcImportStatus::kUnsafeSymlink,
            DiscoverArcSourceAt(application_support_).status);
}

TEST_F(ArcImportFileTest, RejectsSymlinkedSidebarFile) {
  CreateArcRootAndProfile();
  const base::FilePath actual_file =
      temp_dir_.GetPath().Append(FILE_PATH_LITERAL("actual.json"));
  ASSERT_TRUE(base::WriteFile(actual_file, kValidArcSidebar));
  ASSERT_TRUE(base::CreateSymbolicLink(actual_file, SidebarFile()));

  EXPECT_EQ(ArcImportStatus::kUnsafeSymlink,
            DiscoverArcSourceAt(application_support_).status);
}

TEST_F(ArcImportFileTest, RejectsTraversalAndOversizedSource) {
  EXPECT_EQ(
      ArcImportStatus::kInvalidPath,
      DiscoverArcSourceAt(application_support_.Append(FILE_PATH_LITERAL("..")))
          .status);

  CreateArcRootAndProfile();
  ASSERT_TRUE(
      base::WriteFile(SidebarFile(), std::string(kMaxSnapshotBytes + 1, 'x')));
  EXPECT_EQ(ArcImportStatus::kLimitExceeded,
            DiscoverArcSourceAt(application_support_).status);
}

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
  EXPECT_EQ(4u, parsed.plan->stats.imported_page_count);
  EXPECT_EQ(6u, parsed.plan->tree.nodes.size());
}

TEST(ArcImportParserTest, RejectsMalformedSerializedMap) {
  std::string json = kValidArcSidebar;
  ASSERT_TRUE(ReplaceOnce(&json, "\"spaceModels\": [",
                          "\"spaceModels\": [\"dangling\","));
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

}  // namespace

}  // namespace ahoi::importer::arc
