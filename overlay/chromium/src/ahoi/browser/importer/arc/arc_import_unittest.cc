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
#include "ahoi/browser/importer/arc/arc_import_unittest_support.h"
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

using test_support::kValidArcSidebar;
using test_support::ReplaceOnce;
using test_support::SnapshotFor;

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

ArcImportBackupResult CreateArcImportBackupWithoutLiveSourceCheck(
    const base::FilePath& ahoi_profile_path,
    const ArcSource& source,
    const std::string& snapshot_token) {
  return internal::CreateArcImportBackupForTesting(
      ahoi_profile_path, source, snapshot_token,
      base::BindRepeating([](const ArcSource&) { return false; }));
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

}  // namespace

}  // namespace ahoi::importer::arc
