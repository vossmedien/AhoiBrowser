// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_recovery.h"

#include <unistd.h>

#include <string>

#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_manual_recovery_plan.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::arc {

namespace {

constexpr char kSidebar[] = R"json({"version":1})json";

class ArcImportRecoveryTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    profile_path_ = temp_dir_.GetPath().AppendASCII("AhoiProfile");
    arc_root_ = temp_dir_.GetPath().AppendASCII("Arc");
    arc_profile_ = arc_root_.AppendASCII("User Data").AppendASCII("Default");
    sidebar_path_ = arc_root_.AppendASCII("StorableSidebar.json");
    ASSERT_TRUE(base::CreateDirectory(profile_path_));
    ASSERT_TRUE(base::CreateDirectory(arc_profile_));
    ASSERT_TRUE(base::WriteFile(arc_profile_.AppendASCII("Preferences"), "{}"));
    ASSERT_TRUE(base::WriteFile(sidebar_path_, kSidebar));

    tab_tree::TabTreeStore store;
    ASSERT_TRUE(
        store.Initialize(profile_path_.AppendASCII(kTabTreeDatabaseFilename)));
    tab_tree::Workspace workspace;
    workspace.id =
        base::Uuid::ParseLowercase("10000000-0000-4000-8000-000000000001");
    workspace.name = u"Before Arc";
    workspace.icon = u"folder";
    workspace.sort_key = "a";
    workspace.created_at = base::Time::Now();
    workspace.modified_at = workspace.created_at;
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
              store.CreateWorkspace(workspace));
    ASSERT_EQ(tab_tree::TabTreeStore::Result::kOk,
              store.ExportSnapshot(&expected_tree_));
  }

  ArcImportBackupResult CreateBackup() {
    ArcSource source{.arc_root = arc_root_,
                     .browser_profiles = {{.directory_name = "Default",
                                           .path = arc_profile_}},
                     .sidebar_file = sidebar_path_};
    return internal::CreateArcImportBackupForTesting(
        profile_path_, source, SnapshotHash(),
        base::BindRepeating([](const ArcSource&) { return false; }));
  }

  std::string SnapshotHash() const {
    return base::HexEncodeLower(crypto::hash::Sha256(kSidebar));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath profile_path_;
  base::FilePath arc_root_;
  base::FilePath arc_profile_;
  base::FilePath sidebar_path_;
  tab_tree::TabTreeSnapshot expected_tree_;
};

TEST_F(ArcImportRecoveryTest, LoadsExactSnapshotFromVerifiedImmutableBackup) {
  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  ASSERT_FALSE(backup.backup_identifier.empty());
  ASSERT_FALSE(backup.manifest_sha256.empty());

  const ArcImportBackupRecoveryResult recovery =
      VerifyAndLoadArcImportBackup(profile_path_, backup.backup_identifier,
                                   backup.manifest_sha256, SnapshotHash());

  ASSERT_EQ(ArcImportStatus::kOk, recovery.status);
  ASSERT_TRUE(recovery.previous_tree.has_value());
  EXPECT_EQ(expected_tree_, *recovery.previous_tree);
  EXPECT_TRUE(base::PathExists(
      backup.backup_directory.AppendASCII("Ahoi-Tab-Tree.sqlite")));
}

TEST_F(ArcImportRecoveryTest, RejectsPayloadChangedAfterManifestCommit) {
  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  const base::FilePath database =
      backup.backup_directory.AppendASCII("Ahoi-Tab-Tree.sqlite");
  ASSERT_TRUE(base::AppendToFile(database, "tamper"));

  const ArcImportBackupRecoveryResult recovery =
      VerifyAndLoadArcImportBackup(profile_path_, backup.backup_identifier,
                                   backup.manifest_sha256, SnapshotHash());

  EXPECT_EQ(ArcImportStatus::kBackupError, recovery.status);
  EXPECT_FALSE(recovery.previous_tree.has_value());
}

TEST_F(ArcImportRecoveryTest, RejectsHardLinkedBackupPayload) {
  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  const base::FilePath database =
      backup.backup_directory.AppendASCII("Ahoi-Tab-Tree.sqlite");
  const base::FilePath external_link =
      temp_dir_.GetPath().AppendASCII("hard-linked-tab-tree.sqlite");
  ASSERT_EQ(0, link(database.value().c_str(), external_link.value().c_str()));

  const ArcImportBackupRecoveryResult recovery =
      VerifyAndLoadArcImportBackup(profile_path_, backup.backup_identifier,
                                   backup.manifest_sha256, SnapshotHash());

  EXPECT_EQ(ArcImportStatus::kBackupError, recovery.status);
  EXPECT_FALSE(recovery.previous_tree.has_value());
}

TEST_F(ArcImportRecoveryTest, RejectsOverPermissiveManifest) {
  const ArcImportBackupResult backup = CreateBackup();
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  ASSERT_TRUE(base::SetPosixFilePermissions(
      backup.backup_directory.AppendASCII("manifest.json"), 0644));

  const ArcImportBackupRecoveryResult recovery =
      VerifyAndLoadArcImportBackup(profile_path_, backup.backup_identifier,
                                   backup.manifest_sha256, SnapshotHash());

  EXPECT_EQ(ArcImportStatus::kBackupError, recovery.status);
}

TEST_F(ArcImportRecoveryTest,
       ManualPlanPreservesBaselineEditsAndIndependentTemporaryPages) {
  tab_tree::TabTreeSnapshot before = expected_tree_;
  tab_tree::TreeNode baseline_page;
  baseline_page.id =
      base::Uuid::ParseLowercase("20000000-0000-4000-8000-000000000001");
  baseline_page.workspace_id = before.workspaces.front().id;
  baseline_page.type = tab_tree::TreeNodeType::kSavedPage;
  baseline_page.title = u"Before";
  baseline_page.url = GURL("https://example.test/before");
  baseline_page.sort_key = "a";
  baseline_page.created_at = base::Time::Now();
  baseline_page.modified_at = baseline_page.created_at;
  before.nodes.push_back(baseline_page);

  tab_tree::Workspace imported_workspace = before.workspaces.front();
  imported_workspace.id =
      base::Uuid::ParseLowercase("50000000-0000-4000-8000-000000000001");
  imported_workspace.name = u"Imported";
  imported_workspace.sort_key = "b";
  tab_tree::TreeNode imported_page = baseline_page;
  imported_page.id =
      base::Uuid::ParseLowercase("40000000-0000-4000-8000-000000000001");
  imported_page.workspace_id = imported_workspace.id;
  imported_page.url = GURL("https://example.test/imported");
  tab_tree::TabTreeSnapshot expected = before;
  expected.workspaces.push_back(imported_workspace);
  expected.nodes.push_back(imported_page);

  ArcImportPreparedState prepared;
  prepared.phase = ArcImportPreparedPhase::kManualRecoveryRequired;
  prepared.affected_ids = {imported_workspace.id.AsLowercaseString(),
                           imported_page.id.AsLowercaseString()};
  prepared.previous_tree_sha256 = ComputeArcImportTreeFingerprint(before);
  prepared.expected_tree_sha256 = ComputeArcImportTreeFingerprint(expected);

  tab_tree::TabTreeSnapshot current = expected;
  current.nodes.front().title = u"Later local navigation";
  current.nodes.front().url = GURL("https://example.test/after");
  current.nodes.front().modified_at += base::Seconds(1);
  tab_tree::TreeNode temporary = baseline_page;
  temporary.id =
      base::Uuid::ParseLowercase("30000000-0000-4000-8000-000000000001");
  temporary.is_temporary = true;
  temporary.target_kind = sync::SharedTabTargetKind::kWeb;
  current.nodes.push_back(temporary);

  const auto plan = BuildArcImportManualRecoveryPlan(prepared, before, current);
  ASSERT_TRUE(plan);
  tab_tree::TabTreeSnapshot preserved = before;
  preserved.nodes.front() = current.nodes.front();
  preserved.nodes.push_back(temporary);
  EXPECT_EQ(preserved, plan->recovery_tree);
  EXPECT_EQ(std::vector<base::Uuid>({imported_workspace.id}),
            plan->removed_workspaces);
  // A crash after the durable tree write but before journal completion can
  // retry without discarding the same preserved local values or temporary tab.
  const auto retry =
      BuildArcImportManualRecoveryPlan(prepared, before, plan->recovery_tree);
  ASSERT_TRUE(retry);
  EXPECT_EQ(preserved, retry->recovery_tree);

  auto changed_import = current;
  changed_import.nodes[1].title = u"Edited after import";
  EXPECT_FALSE(
      BuildArcImportManualRecoveryPlan(prepared, before, changed_import));
  auto new_saved_page = current;
  new_saved_page.nodes.back().is_temporary = false;
  EXPECT_FALSE(
      BuildArcImportManualRecoveryPlan(prepared, before, new_saved_page));
  auto dependent_temporary = current;
  dependent_temporary.nodes.back().workspace_id = imported_workspace.id;
  EXPECT_FALSE(
      BuildArcImportManualRecoveryPlan(prepared, before, dependent_temporary));
}

}  // namespace

}  // namespace ahoi::importer::arc
