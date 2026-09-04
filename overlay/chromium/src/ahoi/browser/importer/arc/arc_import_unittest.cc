// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
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
            {"unpinned": {"_0": {"shared": {}}}},
            "root-unpinned",
            {"pinned": {}},
            "root-pinned"
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

base::FilePath CreateArcBundleFixture(const base::FilePath& root) {
  const base::FilePath bundle = root.AppendASCII("Arc.app");
  const base::FilePath contents = bundle.AppendASCII("Contents");
  const base::FilePath executable = contents.AppendASCII("MacOS/Arc");
  EXPECT_TRUE(base::CreateDirectory(executable.DirName()));
  EXPECT_TRUE(
      base::WriteFile(contents.AppendASCII("Info.plist"),
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<plist version=\"1.0\"><dict>"
                      "<key>CFBundleIdentifier</key>"
                      "<string>company.thebrowser.Browser</string>"
                      "<key>CFBundleExecutable</key><string>Arc</string>"
                      "</dict></plist>"));
  EXPECT_TRUE(base::WriteFile(executable, "#!/bin/sh\n"));
  EXPECT_TRUE(base::SetPosixFilePermissions(executable, 0755));
  return bundle;
}

std::string CreateBackupFixture(const base::FilePath& backup_root,
                                std::string hash_prefix,
                                base::Time modified) {
  const std::string identifier =
      std::move(hash_prefix) + "-" +
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath directory = backup_root.AppendASCII(identifier);
  EXPECT_TRUE(base::CreateDirectory(directory));
  EXPECT_TRUE(base::SetPosixFilePermissions(directory, 0700));
  constexpr std::string_view kPayload = "{}";
  const std::string payload_hash =
      base::HexEncodeLower(crypto::hash::Sha256(kPayload));
  base::ListValue files;
  for (const auto& [role, source_path, backup_name] : std::initializer_list<
           std::tuple<std::string_view, std::string_view, std::string_view>>{
           {"arc_sidebar", "Arc/StorableSidebar.json",
            "Arc-StorableSidebar.json"},
           {"ahoi_tab_tree", "AhoiProfile/Ahoi Tab Tree",
            "Ahoi-Tab-Tree.sqlite"}}) {
    EXPECT_TRUE(base::WriteFile(directory.AppendASCII(backup_name), kPayload));
    EXPECT_TRUE(base::SetPosixFilePermissions(
        directory.AppendASCII(backup_name), 0600));
    base::DictValue file;
    file.Set("role", role);
    file.Set("source_path", source_path);
    file.Set("backup_name", backup_name);
    file.Set("present", true);
    file.Set("bytes", "2");
    file.Set("modified_unix_ms", "0");
    file.Set("sha256", payload_hash);
    files.Append(std::move(file));
  }
  base::DictValue manifest_value;
  manifest_value.Set("version", 1);
  manifest_value.Set("backup_identifier", identifier);
  manifest_value.Set(
      "snapshot_sha256",
      "1111111111111111111111111111111111111111111111111111111111111111");
  manifest_value.Set("files", std::move(files));
  std::string manifest_json;
  EXPECT_TRUE(base::JSONWriter::Write(manifest_value, &manifest_json));
  const base::FilePath manifest = directory.AppendASCII("manifest.json");
  EXPECT_TRUE(base::WriteFile(manifest, manifest_json));
  EXPECT_TRUE(base::SetPosixFilePermissions(manifest, 0600));
  EXPECT_TRUE(base::TouchFile(directory, modified, modified));
  return identifier;
}

bool SetBackupManifestEntryString(const base::FilePath& backup_directory,
                                  size_t entry_index,
                                  std::string_view key,
                                  std::string value) {
  const base::FilePath manifest = backup_directory.AppendASCII("manifest.json");
  std::string manifest_json;
  if (!base::ReadFileToString(manifest, &manifest_json)) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(manifest_json, base::JSON_PARSE_RFC);
  base::DictValue* manifest_dict =
      parsed.has_value() ? parsed->GetIfDict() : nullptr;
  base::ListValue* files =
      manifest_dict ? manifest_dict->FindList("files") : nullptr;
  base::DictValue* entry = files && entry_index < files->size()
                               ? (*files)[entry_index].GetIfDict()
                               : nullptr;
  if (!entry) {
    return false;
  }
  entry->Set(key, std::move(value));
  return base::JSONWriter::Write(*parsed, &manifest_json) &&
         base::WriteFile(manifest, manifest_json) &&
         base::SetPosixFilePermissions(manifest, 0600);
}

ArcImportSnapshot SnapshotFor(std::string json) {
  ArcImportSnapshot snapshot;
  snapshot.source_size = static_cast<int64_t>(json.size());
  snapshot.sha256 = crypto::hash::Sha256(json);
  snapshot.json = std::move(json);
  return snapshot;
}

ArcImportBackupResult CreateArcImportBackupWithoutLiveSourceCheck(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& source,
    const std::string& snapshot_token) {
  return internal::CreateArcImportBackupForTesting(
      ahoi_profile_path, source, snapshot_token,
      base::BindRepeating([](const ArcSource&) { return false; }));
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

  base::FilePath AhoiProfile() const {
    return temp_dir_.GetPath().Append(FILE_PATH_LITERAL("AhoiProfile"));
  }

  void CreateArcRootAndProfile() {
    ASSERT_TRUE(base::CreateDirectory(BrowserProfile()));
    ASSERT_TRUE(
        base::WriteFile(BrowserProfile().AppendASCII("Preferences"), "{}"));
  }

  void CreateAhoiTreeDatabase() {
    ASSERT_TRUE(base::CreateDirectory(AhoiProfile()));
    tab_tree::TabTreeStore store;
    ASSERT_TRUE(
        store.Initialize(AhoiProfile().AppendASCII(kTabTreeDatabaseFilename)));
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

TEST(ArcImportDiscoveryTest,
     PrefersRunningInjectedBundleFixtureAcrossApplicationRoots) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath system_root = temp_dir.GetPath().AppendASCII("System");
  const base::FilePath user_root = temp_dir.GetPath().AppendASCII("User");
  ASSERT_TRUE(base::CreateDirectory(system_root));
  ASSERT_TRUE(base::CreateDirectory(user_root));
  const base::FilePath system_bundle = CreateArcBundleFixture(system_root);
  const base::FilePath user_bundle = CreateArcBundleFixture(user_root);
  const base::FilePath user_helper = user_bundle.AppendASCII(
      "Contents/Frameworks/Arc Helper.app/Contents/MacOS/Arc Helper");

  const ArcApplicationState state = internal::InspectArcApplicationAtForTesting(
      {system_root, user_root}, {user_helper},
      base::BindRepeating([](const base::FilePath&) { return true; }));

  EXPECT_TRUE(state.installed);
  EXPECT_TRUE(state.running);
  EXPECT_EQ(user_bundle, state.bundle_path);
  EXPECT_NE(system_bundle, state.bundle_path);
}

TEST(ArcImportDiscoveryTest,
     ProductionAuthenticationRejectsUnsignedOfficialIdentityFixture) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath root = temp_dir.GetPath().AppendASCII("Applications");
  ASSERT_TRUE(base::CreateDirectory(root));
  const base::FilePath bundle = CreateArcBundleFixture(root);

  const ArcApplicationState state = internal::InspectArcApplicationAt(
      {root}, {bundle.AppendASCII("Contents/MacOS/Arc")});

  EXPECT_FALSE(state.installed);
  EXPECT_FALSE(state.running);
  EXPECT_TRUE(state.bundle_path.empty());
}

TEST(ArcImportDiscoveryTest, RejectsStructurallyNamedUnauthenticatedBundle) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath root = temp_dir.GetPath().AppendASCII("Applications");
  const base::FilePath fake_executable =
      root.AppendASCII("Arc.app/Contents/MacOS/Arc");
  ASSERT_TRUE(base::CreateDirectory(fake_executable.DirName()));
  ASSERT_TRUE(base::WriteFile(fake_executable, "#!/bin/sh\n"));
  ASSERT_TRUE(base::SetPosixFilePermissions(fake_executable, 0755));
  ASSERT_TRUE(base::WriteFile(
      fake_executable.DirName().DirName().AppendASCII("Info.plist"),
      "<plist><dict><key>CFBundleIdentifier</key>"
      "<string>test.fake.Arc</string><key>CFBundleExecutable</key>"
      "<string>Arc</string></dict></plist>"));

  const ArcApplicationState state =
      internal::InspectArcApplicationAt({root}, {fake_executable});

  EXPECT_FALSE(state.installed);
  EXPECT_FALSE(state.running);
}

TEST(ArcImportDiscoveryTest,
     InspectionFailuresClassifyOnlyRelevantLiveProcessesAsInconclusive) {
  using internal::ProcessLiveness;
  using internal::ProcessOwnership;

  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive));
  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser, ProcessLiveness::kAliveButNotSignalable));
  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser, ProcessLiveness::kUnknown));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser, ProcessLiveness::kExited));

  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kForeignUser, ProcessLiveness::kAlive));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kForeignUser, ProcessLiveness::kAliveButNotSignalable));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kForeignUser, ProcessLiveness::kUnknown));

  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kUnknown, ProcessLiveness::kAlive));
  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kUnknown, ProcessLiveness::kUnknown));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kUnknown, ProcessLiveness::kAliveButNotSignalable));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kUnknown, ProcessLiveness::kExited));
}

TEST(ArcImportDiscoveryTest, InExitFlagNeverProvesThatProcessExited) {
  using internal::ProcessLiveness;
  using internal::ProcessOwnership;

  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser,
      ProcessLiveness::kInExitWithoutExitEvidence));
  EXPECT_TRUE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kUnknown, ProcessLiveness::kInExitWithoutExitEvidence));
  EXPECT_FALSE(internal::IsProcessInspectionFailureInconclusive(
      ProcessOwnership::kCurrentUser, ProcessLiveness::kExited));
}

TEST(ArcImportDiscoveryTest,
     OpenFileInspectionRetriesChurnAndDoesNotAttributeReusedPid) {
  using internal::DecideOpenFileInspectionFailure;
  using internal::ProcessIdentityMatch;
  using internal::ProcessInspectionFailureDisposition;
  using internal::ProcessLiveness;
  using internal::ProcessOwnership;

  EXPECT_EQ(ProcessInspectionFailureDisposition::kRetry,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive,
                ProcessIdentityMatch::kSameProcess,
                /*consecutive_same_process_failures=*/1,
                /*can_retry=*/true));
  EXPECT_EQ(
      ProcessInspectionFailureDisposition::kRetry,
      DecideOpenFileInspectionFailure(ProcessOwnership::kForeignUser,
                                      ProcessLiveness::kAliveButNotSignalable,
                                      ProcessIdentityMatch::kSameProcess,
                                      /*consecutive_same_process_failures=*/1,
                                      /*can_retry=*/true));
  EXPECT_EQ(ProcessInspectionFailureDisposition::kRetry,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive,
                ProcessIdentityMatch::kSameProcess,
                /*consecutive_same_process_failures=*/2,
                /*can_retry=*/true));
  EXPECT_EQ(ProcessInspectionFailureDisposition::kInconclusive,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive,
                ProcessIdentityMatch::kSameProcess,
                /*consecutive_same_process_failures=*/3,
                /*can_retry=*/false));
  EXPECT_EQ(
      ProcessInspectionFailureDisposition::kIgnore,
      DecideOpenFileInspectionFailure(ProcessOwnership::kForeignUser,
                                      ProcessLiveness::kAliveButNotSignalable,
                                      ProcessIdentityMatch::kSameProcess,
                                      /*consecutive_same_process_failures=*/3,
                                      /*can_retry=*/false));
  EXPECT_EQ(ProcessInspectionFailureDisposition::kIgnore,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive,
                ProcessIdentityMatch::kDifferentProcess,
                /*consecutive_same_process_failures=*/0,
                /*can_retry=*/true));
  EXPECT_EQ(ProcessInspectionFailureDisposition::kInconclusive,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kAlive,
                ProcessIdentityMatch::kUnknown,
                /*consecutive_same_process_failures=*/0,
                /*can_retry=*/false));
  EXPECT_EQ(
      ProcessInspectionFailureDisposition::kIgnore,
      DecideOpenFileInspectionFailure(ProcessOwnership::kForeignUser,
                                      ProcessLiveness::kAliveButNotSignalable,
                                      ProcessIdentityMatch::kUnknown,
                                      /*consecutive_same_process_failures=*/0,
                                      /*can_retry=*/false));
  EXPECT_EQ(ProcessInspectionFailureDisposition::kIgnore,
            DecideOpenFileInspectionFailure(
                ProcessOwnership::kCurrentUser, ProcessLiveness::kExited,
                ProcessIdentityMatch::kSameProcess,
                /*consecutive_same_process_failures=*/3,
                /*can_retry=*/false));
}

TEST(ArcImportDiscoveryTest,
     OnlyPositiveRelevantHandleEvidenceBlocksTheSource) {
  EXPECT_TRUE(internal::ShouldBlockOnOpenFileInspectionEvidence(
      internal::OpenFileInspectionEvidence::kRelevantSourceHandle,
      internal::ProcessOwnership::kForeignUser));
  EXPECT_FALSE(internal::ShouldBlockOnOpenFileInspectionEvidence(
      internal::OpenFileInspectionEvidence::kNoRelevantSourceHandle,
      internal::ProcessOwnership::kForeignUser));
  EXPECT_FALSE(internal::ShouldBlockOnOpenFileInspectionEvidence(
      internal::OpenFileInspectionEvidence::kNoRelevantSourceHandle,
      internal::ProcessOwnership::kCurrentUser));
  EXPECT_FALSE(internal::ShouldBlockOnOpenFileInspectionEvidence(
      internal::OpenFileInspectionEvidence::kNoRelevantSourceHandle,
      internal::ProcessOwnership::kUnknown));
  EXPECT_FALSE(internal::ShouldBlockOnOpenFileInspectionEvidence(
      internal::OpenFileInspectionEvidence::kInspectionInconclusive,
      internal::ProcessOwnership::kCurrentUser));
}

TEST_F(ArcImportFileTest, ProtectsSidebarAndSelectedProfileDatabaseFiles) {
  CreateArcRootAndProfile();
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());
  const ArcSource& source = *discovery.source;

  EXPECT_TRUE(internal::IsRelevantArcSourcePath(source, source.sidebar_file));
  for (std::string_view filename :
       {"Preferences", "Bookmarks", "History", "History-wal", "History-shm",
        "Favicons", "Favicons-wal", "Favicons-shm", "Web Data", "Web Data-wal",
        "Web Data-shm"}) {
    EXPECT_TRUE(internal::IsRelevantArcSourcePath(
        source, BrowserProfile().AppendASCII(filename)))
        << filename;
  }

  EXPECT_FALSE(internal::IsRelevantArcSourcePath(
      source, BrowserProfile().AppendASCII("Cache/Cache_Data/index")));
  EXPECT_FALSE(internal::IsRelevantArcSourcePath(
      source, BrowserProfile().AppendASCII("History-journal")));
  EXPECT_FALSE(
      internal::IsRelevantArcSourcePath(source, BrowserProfile()
                                                    .DirName()
                                                    .AppendASCII("Profile 2")
                                                    .AppendASCII("History")));
}

TEST_F(ArcImportFileTest, CreatesVerifiedOwnerOnlyBackupFromStableGeneration) {
  CreateArcRootAndProfile();
  CreateAhoiTreeDatabase();
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  ASSERT_TRUE(base::WriteFile(BrowserProfile().AppendASCII("Bookmarks"),
                              R"json({"roots":{}})json"));
  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());
  const std::string token =
      base::HexEncodeLower(crypto::hash::Sha256(kValidArcSidebar));

  const ArcImportBackupResult backup =
      CreateArcImportBackupWithoutLiveSourceCheck(AhoiProfile(),
                                                  *discovery.source, token);

  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  EXPECT_TRUE(base::PathExists(
      backup.backup_directory.AppendASCII("Arc-StorableSidebar.json")));
  EXPECT_TRUE(
      base::PathExists(backup.backup_directory.AppendASCII("manifest.json")));
  int permissions = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(
      backup.backup_directory.AppendASCII("Arc-StorableSidebar.json"),
      &permissions));
  EXPECT_EQ(0600, permissions & 0777);
}

TEST_F(ArcImportFileTest, BackupRejectsFifoWithoutBlocking) {
  CreateArcRootAndProfile();
  ASSERT_TRUE(base::CreateDirectory(AhoiProfile()));
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  ASSERT_EQ(0, mkfifo(BrowserProfile().AppendASCII("Bookmarks").value().c_str(),
                      0600));
  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());

  const ArcImportBackupResult backup =
      CreateArcImportBackupWithoutLiveSourceCheck(
          AhoiProfile(), *discovery.source,
          base::HexEncodeLower(crypto::hash::Sha256(kValidArcSidebar)));

  EXPECT_EQ(ArcImportStatus::kBackupError, backup.status);
  EXPECT_TRUE(backup.backup_directory.empty());
}

TEST_F(ArcImportFileTest, BackupRejectsLeafSymlinkInsteadOfFollowingIt) {
  CreateArcRootAndProfile();
  ASSERT_TRUE(base::CreateDirectory(AhoiProfile()));
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  const base::FilePath actual =
      temp_dir_.GetPath().AppendASCII("actual-bookmarks.json");
  ASSERT_TRUE(base::WriteFile(actual, R"json({"roots":{}})json"));
  ASSERT_TRUE(base::CreateSymbolicLink(
      actual, BrowserProfile().AppendASCII("Bookmarks")));
  const ArcDiscoveryResult discovery =
      DiscoverArcSourceAt(application_support_);
  ASSERT_EQ(ArcImportStatus::kOk, discovery.status);
  ASSERT_TRUE(discovery.source.has_value());

  const ArcImportBackupResult backup =
      CreateArcImportBackupWithoutLiveSourceCheck(
          AhoiProfile(), *discovery.source,
          base::HexEncodeLower(crypto::hash::Sha256(kValidArcSidebar)));

  EXPECT_EQ(ArcImportStatus::kBackupError, backup.status);
  EXPECT_TRUE(backup.backup_directory.empty());
}

TEST_F(ArcImportFileTest, BackupNamesRemainUniqueForSimilarProfileNames) {
  CreateAhoiTreeDatabase();
  ASSERT_TRUE(
      base::CreateDirectory(ArcRoot().Append(FILE_PATH_LITERAL("User Data"))));
  ASSERT_TRUE(base::WriteFile(SidebarFile(), kValidArcSidebar));
  ArcSource source{.arc_root = ArcRoot(), .sidebar_file = SidebarFile()};
  for (std::string name : {"A B", "A_B"}) {
    const base::FilePath profile = BrowserProfile().DirName().AppendASCII(name);
    ASSERT_TRUE(base::CreateDirectory(profile));
    ASSERT_TRUE(base::WriteFile(profile.AppendASCII("Bookmarks"), name));
    source.browser_profiles.push_back(
        {.directory_name = std::move(name), .path = profile});
  }

  const ArcImportBackupResult backup =
      CreateArcImportBackupWithoutLiveSourceCheck(
          AhoiProfile(), source,
          base::HexEncodeLower(crypto::hash::Sha256(kValidArcSidebar)));

  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  const std::string first_key =
      base::HexEncodeLower(crypto::hash::Sha256("A B"));
  const std::string second_key =
      base::HexEncodeLower(crypto::hash::Sha256("A_B"));
  EXPECT_NE(first_key, second_key);
  EXPECT_TRUE(base::PathExists(backup.backup_directory.AppendASCII(
      "Arc-" + first_key + "-Bookmarks.json")));
  EXPECT_TRUE(base::PathExists(backup.backup_directory.AppendASCII(
      "Arc-" + second_key + "-Bookmarks.json")));
}

TEST(ArcImportBackupResourceTest, RejectsQuotaOverflowAndLowFreeSpace) {
  ArcImportBackupLimits limits;
  limits.max_total_bytes = 100;
  limits.max_file_count = 3;
  limits.minimum_free_headroom_bytes = 20;
  uint64_t total = 0;

  EXPECT_EQ(
      ArcImportStatus::kBackupQuotaExceeded,
      internal::CheckArcImportBackupResources({60, 41}, 1000, limits, &total));
  EXPECT_EQ(ArcImportStatus::kBackupQuotaExceeded,
            internal::CheckArcImportBackupResources({1, 1, 1, 1}, 1000, limits,
                                                    &total));
  EXPECT_EQ(ArcImportStatus::kInsufficientDiskSpace,
            internal::CheckArcImportBackupResources({60}, 79, limits, &total));
  EXPECT_EQ(ArcImportStatus::kOk,
            internal::CheckArcImportBackupResources({60}, 80, limits, &total));
  EXPECT_EQ(60u, total);
}

TEST(ArcImportBackupRetentionTest,
     PreservesPreparedForeignAndSymlinkEntriesWhilePruningOwnedBackups) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath root = temp_dir.GetPath().AppendASCII("backups");
  ASSERT_TRUE(base::CreateDirectory(root));
  ASSERT_TRUE(base::SetPosixFilePermissions(root, 0700));
  const base::Time now = base::Time::Now();
  const std::string protected_id =
      CreateBackupFixture(root, "aaaaaaaaaaaa", now - base::Days(4));
  const std::string old_id =
      CreateBackupFixture(root, "bbbbbbbbbbbb", now - base::Days(3));
  const std::string middle_id =
      CreateBackupFixture(root, "cccccccccccc", now - base::Days(2));
  const std::string newest_id =
      CreateBackupFixture(root, "dddddddddddd", now - base::Days(1));
  const base::FilePath foreign = root.AppendASCII("foreign-backup");
  ASSERT_TRUE(base::CreateDirectory(foreign));
  const base::FilePath invalid_manifest =
      root.AppendASCII("ffffffffffff-22222222-2222-4222-8222-222222222222");
  ASSERT_TRUE(base::CreateDirectory(invalid_manifest));
  ASSERT_TRUE(base::SetPosixFilePermissions(invalid_manifest, 0700));
  ASSERT_TRUE(
      base::WriteFile(invalid_manifest.AppendASCII("manifest.json"), "{}"));
  ASSERT_TRUE(base::SetPosixFilePermissions(
      invalid_manifest.AppendASCII("manifest.json"), 0600));
  const base::FilePath outside = temp_dir.GetPath().AppendASCII("outside");
  ASSERT_TRUE(base::CreateDirectory(outside));
  const base::FilePath symlink =
      root.AppendASCII("eeeeeeeeeeee-11111111-1111-4111-8111-111111111111");
  ASSERT_TRUE(base::CreateSymbolicLink(outside, symlink));

  ASSERT_TRUE(internal::PruneArcImportBackupsForTesting(
      root, {protected_id}, /*max_retained_backups=*/2));

  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(protected_id)));
  EXPECT_FALSE(base::PathExists(root.AppendASCII(old_id)));
  EXPECT_FALSE(base::PathExists(root.AppendASCII(middle_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(newest_id)));
  EXPECT_TRUE(base::DirectoryExists(foreign));
  EXPECT_TRUE(base::DirectoryExists(invalid_manifest));
  EXPECT_TRUE(base::IsLink(symlink));
}

TEST(ArcImportBackupRetentionTest,
     DeletesOnlyContentVerifiedBackupsAndProtectsPreparedJournalIdentifier) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath root = temp_dir.GetPath().AppendASCII("backups");
  ASSERT_TRUE(base::CreateDirectory(root));
  ASSERT_TRUE(base::SetPosixFilePermissions(root, 0700));
  const base::Time now = base::Time::Now();
  const std::string valid_id =
      CreateBackupFixture(root, "111111111111", now - base::Days(7));
  const std::string prepared_id =
      CreateBackupFixture(root, "222222222222", now - base::Days(6));
  const std::string wrong_hash_id =
      CreateBackupFixture(root, "333333333333", now - base::Days(5));
  const std::string wrong_size_id =
      CreateBackupFixture(root, "444444444444", now - base::Days(4));
  const std::string extra_file_id =
      CreateBackupFixture(root, "555555555555", now - base::Days(3));
  const std::string symlink_id =
      CreateBackupFixture(root, "666666666666", now - base::Days(2));
  const std::string hardlink_id =
      CreateBackupFixture(root, "777777777777", now - base::Days(1));

  ASSERT_TRUE(base::WriteFile(
      root.AppendASCII(wrong_hash_id).AppendASCII("Arc-StorableSidebar.json"),
      "[]"));
  ASSERT_TRUE(SetBackupManifestEntryString(root.AppendASCII(wrong_size_id), 0,
                                           "bytes", "1"));
  const base::FilePath extra_file =
      root.AppendASCII(extra_file_id).AppendASCII("Arc-Unlisted.json");
  ASSERT_TRUE(base::WriteFile(extra_file, "{}"));
  ASSERT_TRUE(base::SetPosixFilePermissions(extra_file, 0600));

  const base::FilePath outside = temp_dir.GetPath().AppendASCII("outside");
  ASSERT_TRUE(base::WriteFile(outside, "{}"));
  ASSERT_TRUE(base::SetPosixFilePermissions(outside, 0600));
  const base::FilePath symlink_payload =
      root.AppendASCII(symlink_id).AppendASCII("Arc-StorableSidebar.json");
  ASSERT_TRUE(base::DeleteFile(symlink_payload));
  ASSERT_TRUE(base::CreateSymbolicLink(outside, symlink_payload));

  const base::FilePath hardlink_payload =
      root.AppendASCII(hardlink_id).AppendASCII("Arc-StorableSidebar.json");
  ASSERT_TRUE(base::DeleteFile(hardlink_payload));
  ASSERT_EQ(0, link(outside.value().c_str(), hardlink_payload.value().c_str()));

  ASSERT_TRUE(internal::PruneArcImportBackupsForTesting(
      root, {prepared_id}, /*max_retained_backups=*/0));

  EXPECT_FALSE(base::PathExists(root.AppendASCII(valid_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(prepared_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(wrong_hash_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(wrong_size_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(extra_file_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(symlink_id)));
  EXPECT_TRUE(base::DirectoryExists(root.AppendASCII(hardlink_id)));
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
