// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_service.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_commit_support.h"
#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "ahoi/browser/importer/arc/arc_import_transaction_key.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/importer/arc/arc_split_receipt.h"
#include "ahoi/browser/importer/arc/arc_split_runtime.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "crypto/hash.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ahoi::importer::arc {

// The only substituted boundary is locating an installed Arc application and
// the user's Application Support directory. The pending plan and token come
// from genuine bounded discovery/capture/parsing of closed fixture files.
// Public Commit still executes the production source-use, generation, backup,
// journal, tree persistence and native-session verification workers.
class ArcImportServiceTestPeer {
 public:
  static ArcImportStatus SeedFromFiles(ArcImportService* service,
                                       const base::FilePath& source_root,
                                       ArcImportPlan* plan,
                                       std::string* token) {
    if (!service || service->operation_in_progress_ || !service->profile_ ||
        !plan || !token) {
      return ArcImportStatus::kTransactionFailed;
    }
    const auto journal = ReadArcImportJournal(service->profile_->GetPath());
    if (journal.status != ArcImportStatus::kOk) {
      return journal.status;
    }
    if (journal.state == ArcImportJournalState::kPrepared) {
      return ArcImportStatus::kRecoveryRequired;
    }
    const auto discovery = DiscoverArcSourceAt(source_root);
    if (discovery.status != ArcImportStatus::kOk || !discovery.source) {
      return discovery.status;
    }
    const auto snapshot = CaptureArcSnapshot(*discovery.source);
    if (snapshot.status != ArcImportStatus::kOk || !snapshot.snapshot) {
      return snapshot.status;
    }
    const auto parsed = ParseArcSnapshot(*snapshot.snapshot);
    if (parsed.status != ArcImportStatus::kOk || !parsed.plan) {
      return parsed.status;
    }
    *plan = *parsed.plan;
    *token = ArcImportSnapshotToken(*snapshot.snapshot);
    service->pending_source_ = *discovery.source;
    service->pending_plan_ = *parsed.plan;
    service->pending_snapshot_token_ = *token;
    service->committed_journal_state_ = journal.committed;
    return ArcImportStatus::kOk;
  }
};

namespace {

using Store = tab_tree::TabTreeStore;

std::string SidebarFixture(const GURL& first, const GURL& second) {
  // Same schema-1 serialized-map/root/split representation exercised by the
  // parser fixtures, reduced to one workspace and one two-member split.
  return base::StringPrintf(R"json({
    "version":1,
    "sidebarSyncState":{
      "container":{"value":{"version":6,"orderedSpaceIDs":["space-a"]}},
      "spaceModels":["space-a",{"value":{
        "id":"space-a","title":"Imported workspace",
        "newContainerIDs":[{"pinned":{}},"root-pinned",
          {"unpinned":{"_0":{"shared":{}}}},"root-unpinned"]
      }}],
      "items":[
        "root-pinned",{"value":{"id":"root-pinned","parentID":null,
          "childrenIds":[],"title":null,
          "data":{"itemContainer":{"containerType":{
            "spaceItems":{"_0":"space-a"}}}}}},
        "root-unpinned",{"value":{"id":"root-unpinned","parentID":null,
          "childrenIds":["split-a"],"title":null,
          "data":{"itemContainer":{"containerType":{
            "spaceItems":{"_0":"space-a"}}}}}},
        "split-a",{"value":{"id":"split-a","parentID":"root-unpinned",
          "childrenIds":["first","second"],"title":"Imported split",
          "data":{"splitView":{"layoutOrientation":"horizontal",
            "focusItemID":"second",
            "itemWidthFactors":["first",0.4,"second",0.6]}}}},
        "first",{"value":{"id":"first","parentID":"split-a",
          "childrenIds":[],"title":"Imported first",
          "data":{"tab":{"savedTitle":"Imported first","savedURL":"%s"}}}},
        "second",{"value":{"id":"second","parentID":"split-a",
          "childrenIds":[],"title":"Imported second",
          "data":{"tab":{"savedTitle":"Imported second","savedURL":"%s"}}}}
      ]
    }
  })json",
                            first.spec().c_str(), second.spec().c_str());
}

ArcImportSelection ConfirmedSelection() {
  return {.import_sidebar = true,
          .reconstruct_splits = true,
          .backup_confirmed = true,
          .commit_confirmed = true,
          .selected_browser_profiles = {"Default"}};
}

class ArcImportServiceBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (IsArcApplicationRunning()) {
      GTEST_SKIP()
          << "Real Arc is running; production source-use gate retained";
    }
    ASSERT_TRUE(source_directory_.CreateUniqueTempDir());
    const base::FilePath arc_profile = source_directory_.GetPath()
                                           .AppendASCII("Arc")
                                           .AppendASCII("User Data")
                                           .AppendASCII("Default");
    ASSERT_TRUE(base::CreateDirectory(arc_profile));
    ASSERT_TRUE(base::SetPosixFilePermissions(arc_profile, 0700));
    ASSERT_TRUE(base::WriteFile(arc_profile.AppendASCII("Preferences"), "{}"));
    first_response_ =
        std::make_unique<net::test_server::ControllableHttpResponse>(
            embedded_test_server(), "/arc/first");
    second_response_ =
        std::make_unique<net::test_server::ControllableHttpResponse>(
            embedded_test_server(), "/arc/second");
    ASSERT_TRUE(embedded_test_server()->Start());
    sidebar_json_ =
        SidebarFixture(embedded_test_server()->GetURL("/arc/first"),
                       embedded_test_server()->GetURL("/arc/second"));
    ASSERT_TRUE(base::WriteFile(SidebarPath(), sidebar_json_));
    ASSERT_TRUE(base::SetPosixFilePermissions(SidebarPath(), 0600));

    ASSERT_TRUE(browser()->GetBrowserView().IsAhoiBrowserSurface());
    bridge_ = SessionBridgeFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(bridge_);
    ASSERT_TRUE(bridge_->is_operational());
    base::RunLoop ready;
    bridge_->RunWhenReadyForTesting(ready.QuitClosure());
    ready.Run();
    ASSERT_TRUE(bridge_->is_ready());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/importData")));

    const auto workspace_id = bridge_->GetActiveWorkspaceForWindow(browser());
    ASSERT_TRUE(workspace_id);
    const auto now = base::Time::UnixEpoch() + base::Seconds(10);
    tab_tree::TreeNode folder{
        .id =
            base::Uuid::ParseLowercase("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"),
        .workspace_id = *workspace_id,
        .type = tab_tree::TreeNodeType::kFolder,
        .title = u"Existing folder",
        .icon = u"code",
        .sort_key = "z-folder",
        .created_at = now,
        .modified_at = now,
    };
    tab_tree::TreeNode child{
        .id =
            base::Uuid::ParseLowercase("00000000-0000-4000-8000-000000000001"),
        .workspace_id = *workspace_id,
        .parent_id = folder.id,
        .type = tab_tree::TreeNodeType::kSavedPage,
        .title = u"Before rename",
        .url = GURL("https://local-saved.example.test/"),
        .sort_key = "a-child",
        .created_at = now,
        .modified_at = now,
    };
    ASSERT_EQ(Store::Result::kOk,
              bridge_->tab_tree_store()->CreateNode(folder));
    ASSERT_EQ(Store::Result::kOk, bridge_->tab_tree_store()->CreateNode(child));
    ASSERT_EQ(Store::Result::kOk,
              bridge_->tab_tree_store()->RenameNode(
                  child.id, u"Existing renamed child", now + base::Seconds(1)));
    ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&previous_tree_));
    ASSERT_FALSE(previous_tree_.undo_operations.empty());
    base::test::TestFuture<bool> flushed;
    bridge_->FlushPersistenceForBackup(flushed.GetCallback());
    ASSERT_TRUE(flushed.Get());

    service_ =
        std::make_unique<ArcImportService>(browser()->GetProfile(), bridge_);
    ASSERT_EQ(
        ArcImportStatus::kOk,
        ArcImportServiceTestPeer::SeedFromFiles(
            service_.get(), source_directory_.GetPath(), &plan_, &token_));
    ASSERT_EQ(1u, plan_.splits.size());
    ASSERT_EQ(2u, plan_.splits.front().member_node_ids.size());
    ASSERT_EQ(1u, plan_.tree.workspaces.size());
    initial_tab_count_ = browser()->tab_strip_model()->count();
  }

  void TearDownOnMainThread() override {
    if (service_) {
      service_->Shutdown();
      service_.reset();
    }
    first_response_.reset();
    second_response_.reset();
    bridge_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  base::FilePath SidebarPath() const {
    return source_directory_.GetPath().AppendASCII("Arc").AppendASCII(
        "StorableSidebar.json");
  }

  void PrepareFailedTreeOnlyImport() {
    // Reproduce the on-disk state left by the old canonical-order bug, using
    // real backups, tree persistence and journal I/O, not recovery callbacks.
    const auto discovery = DiscoverArcSourceAt(source_directory_.GetPath());
    ASSERT_TRUE(discovery.source);
    const auto backup = CreateArcImportBackup(
        browser()->GetProfile()->GetPath(), *discovery.source, token_);
    ASSERT_EQ(ArcImportStatus::kOk, backup.status);
    const auto merged = MergeArcImportPlan(previous_tree_, plan_,
                                           ArcConflictResolution::kRename);
    ASSERT_EQ(ArcImportStatus::kOk, merged.status);
    ASSERT_TRUE(merged.merged_tree);
    ASSERT_TRUE(merged.applied_plan);
    prepared_.transaction_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    prepared_.snapshot_hash = token_;
    prepared_.selection_fingerprint = ComputeArcImportSelectionFingerprint(
        {.selected_browser_profiles = {"Default"}});
    prepared_.idempotency_key =
        ComputeArcImportIdempotencyKey(token_, prepared_.selection_fingerprint);
    prepared_.backup_identifier = backup.backup_identifier;
    prepared_.manifest_sha256 = backup.manifest_sha256;
    prepared_.previous_tree_sha256 =
        ComputeArcImportTreeFingerprint(previous_tree_);
    prepared_.expected_tree_sha256 =
        ComputeArcImportTreeFingerprint(*merged.merged_tree);
    prepared_.affected_ids = ArcImportAffectedIds(*merged.applied_plan);
    prepared_.runtime_mutation_planned = true;
    prepared_.expected_native_structure_sha256 =
        ComputeArcSplitStructureFingerprint(*merged.applied_plan);
    for (const auto& id : plan_.splits.front().member_node_ids) {
      prepared_.native_member_ids.push_back(id.AsLowercaseString());
    }
    std::ranges::sort(prepared_.native_member_ids);
    prepared_.phase = ArcImportPreparedPhase::kManualRecoveryRequired;
    ASSERT_TRUE(WriteArcImportPreparedJournal(
        browser()->GetProfile()->GetPath(), prepared_));
    ASSERT_EQ(Store::Result::kOk,
              bridge_->ApplySyncedTabTreeSnapshot(*merged.merged_tree));
    base::test::TestFuture<bool> flushed;
    bridge_->FlushPersistenceForBackup(flushed.GetCallback());
    ASSERT_TRUE(flushed.Get());
  }

  void ExpectRecoveryRejectedWithoutTreeOrJournalChange() {
    tab_tree::TabTreeSnapshot before;
    ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&before));
    const auto journal =
        ReadArcImportJournal(browser()->GetProfile()->GetPath());
    base::test::TestFuture<ArcImportPreview> result;
    service_->RecoverFailedImport(result.GetCallback());
    ASSERT_TRUE(result.Wait());
    EXPECT_EQ(ArcImportStatus::kRecoveryRequired, result.Get().status);
    EXPECT_FALSE(service_->operation_in_progress());
    tab_tree::TabTreeSnapshot after;
    ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&after));
    EXPECT_EQ(before, after);
    EXPECT_EQ(
        journal.prepared,
        ReadArcImportJournal(browser()->GetProfile()->GetPath()).prepared);
  }

  std::vector<base::FilePath> BackupDirectories() const {
    std::vector<base::FilePath> backups;
    base::FileEnumerator directories(
        browser()->GetProfile()->GetPath().AppendASCII("Ahoi").AppendASCII(
            "Arc Import Backups"),
        false, base::FileEnumerator::DIRECTORIES);
    for (auto path = directories.Next(); !path.empty();
         path = directories.Next()) {
      backups.push_back(std::move(path));
    }
    std::ranges::sort(backups);
    return backups;
  }

  base::ScopedTempDir source_directory_;
  std::unique_ptr<net::test_server::ControllableHttpResponse> first_response_;
  std::unique_ptr<net::test_server::ControllableHttpResponse> second_response_;
  raw_ptr<SessionBridge> bridge_ = nullptr;
  std::unique_ptr<ArcImportService> service_;
  tab_tree::TabTreeSnapshot previous_tree_;
  ArcImportPlan plan_;
  ArcImportPreparedState prepared_;
  std::string token_;
  std::string sidebar_json_;
  int initial_tab_count_ = 0;
};

IN_PROC_BROWSER_TEST_F(
    ArcImportServiceBrowserTest,
    CommitsSlowNativeSplitThroughRealBackupAndSessionReceipt) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const auto expected =
      MergeArcImportPlan(previous_tree_, plan_, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, expected.status);
  ASSERT_TRUE(expected.merged_tree);
  ASSERT_TRUE(expected.applied_plan);
  auto append_order = previous_tree_;
  append_order.workspaces.insert(append_order.workspaces.end(),
                                 plan_.tree.workspaces.begin(),
                                 plan_.tree.workspaces.end());
  append_order.nodes.insert(append_order.nodes.end(), plan_.tree.nodes.begin(),
                            plan_.tree.nodes.end());
  ASSERT_NE(append_order, *expected.merged_tree)
      << "Fixture must exercise canonical ordering, not append-order equality";
  Browser* const foreground = CreateBrowser(browser()->GetProfile());
  ui_test_utils::WaitUntilBrowserBecomeActive(foreground);
  ASSERT_TRUE(foreground->IsActive());
  ASSERT_FALSE(browser()->IsActive());

  base::test::TestFuture<ArcImportCommitResult> committed;
  service_->Commit(token_, ArcConflictResolution::kRename, ConfirmedSelection(),
                   browser(), committed.GetCallback());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return committed.IsReady() || (first_response_->has_received_request() &&
                                   second_response_->has_received_request());
  }));
  ASSERT_FALSE(committed.IsReady()) << "Import ended before native navigation";
  first_response_->WaitForRequest();
  second_response_->WaitForRequest();
  // Drain ready journal/session I/O without letting either navigation commit.
  // A fixed sleep or preloaded/prebound tab would conceal a premature receipt.
  content::RunAllTasksUntilIdle();
  ASSERT_FALSE(committed.IsReady());
  EXPECT_TRUE(service_->operation_in_progress());
  const auto pending = ReadArcImportJournal(browser()->GetProfile()->GetPath());
  ASSERT_EQ(ArcImportStatus::kOk, pending.status);
  ASSERT_EQ(ArcImportJournalState::kPrepared, pending.state);
  ASSERT_TRUE(pending.prepared);
  EXPECT_EQ(ArcImportPreparedPhase::kRuntimeMayHaveStarted,
            pending.prepared->phase);
  EXPECT_TRUE(pending.prepared->native_receipt_sha256.empty());
  ASSERT_EQ(2u, pending.prepared->native_member_ids.size());

  // Commit real HTTP navigations, but keep both streams open. Native session
  // readiness must not require complete page loading or network quiescence.
  const std::string padding(2048, ' ');
  first_response_->Send(
      net::HTTP_OK, "text/html",
      "<!doctype html><title>Imported first</title><p>First</p>" + padding);
  second_response_->Send(
      net::HTTP_OK, "text/html",
      "<!doctype html><title>Imported second</title><p>Second</p>" + padding);
  ASSERT_TRUE(committed.Wait());
  ASSERT_EQ(ArcImportStatus::kOk, committed.Get().status);
  EXPECT_EQ(1u, committed.Get().reconstructed_split_count);
  first_response_->Done();
  second_response_->Done();
  EXPECT_FALSE(service_->operation_in_progress());
  EXPECT_TRUE(foreground->IsActive());
  EXPECT_FALSE(browser()->IsActive());
  ASSERT_EQ(initial_tab_count_ + 2, browser()->tab_strip_model()->count());
  EXPECT_EQ(ArcSplitVerification::kExact,
            VerifyArcSplitRuntime(browser(), bridge_, *expected.applied_plan,
                                  /*require_focus=*/true));
  EXPECT_EQ(plan_.tree.workspaces.front().id,
            bridge_->GetActiveWorkspaceForWindow(browser()));
  std::vector<base::WeakPtr<tabs::TabInterface>> members;
  for (const auto& id : plan_.splits.front().member_node_ids) {
    auto* const tab = bridge_->FindTabByTreeNodeId(id);
    ASSERT_TRUE(tab);
    members.push_back(tab->GetWeakPtr());
  }

  tab_tree::TabTreeSnapshot live;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&live));
  EXPECT_EQ(*expected.merged_tree, live);
  EXPECT_EQ(previous_tree_.undo_operations, live.undo_operations);
  const auto journal = ReadArcImportJournal(browser()->GetProfile()->GetPath());
  ASSERT_EQ(ArcImportStatus::kOk, journal.status);
  ASSERT_EQ(ArcImportJournalState::kCommitted, journal.state);
  ASSERT_TRUE(journal.committed);
  EXPECT_EQ(token_, journal.committed->snapshot_hash);
  EXPECT_EQ(1, journal.committed->metrics.reconstructed_splits);
  const auto backups = BackupDirectories();
  ASSERT_EQ(1u, backups.size());
  std::string manifest;
  ASSERT_TRUE(base::ReadFileToString(
      backups.front().AppendASCII("manifest.json"), &manifest));
  const auto backup = VerifyAndLoadArcImportBackup(
      browser()->GetProfile()->GetPath(),
      backups.front().BaseName().MaybeAsASCII(),
      base::HexEncodeLower(crypto::hash::Sha256(manifest)), token_);
  ASSERT_EQ(ArcImportStatus::kOk, backup.status);
  ASSERT_TRUE(backup.previous_tree);
  EXPECT_EQ(previous_tree_, *backup.previous_tree);
  std::string unchanged_source;
  ASSERT_TRUE(base::ReadFileToString(SidebarPath(), &unchanged_source));
  EXPECT_EQ(sidebar_json_, unchanged_source);

  base::test::TestFuture<bool> flushed;
  bridge_->FlushPersistenceForBackup(flushed.GetCallback());
  ASSERT_TRUE(flushed.Get());
  const auto durable_copy =
      source_directory_.GetPath().AppendASCII("durable-copy");
  ASSERT_TRUE(base::CopyFile(
      browser()->GetProfile()->GetPath().AppendASCII(kTabTreeDatabaseFilename),
      durable_copy));
  Store durable_store;
  ASSERT_TRUE(durable_store.Initialize(durable_copy));
  tab_tree::TabTreeSnapshot durable;
  ASSERT_EQ(Store::Result::kOk, durable_store.ExportSnapshot(&durable));
  EXPECT_EQ(live, durable);

  // Independently read the real native current-session file again. Do not feed
  // hand-built SessionWindows to the verifier in this orchestration test.
  auto* const session_service =
      SessionServiceFactory::GetForProfileIfExisting(browser()->GetProfile());
  ASSERT_TRUE(session_service);
  base::test::TestFuture<std::vector<std::unique_ptr<sessions::SessionWindow>>,
                         SessionID, bool>
      native;
  session_service->ResetFlushAndReadCurrentSessionForVerification(
      native.GetCallback());
  ASSERT_TRUE(native.Wait());
  EXPECT_FALSE(native.Get<2>());
  EXPECT_EQ(foreground->GetSessionID(), native.Get<1>());
  const auto receipt = VerifyArcSplitSessionWindows(
      *expected.applied_plan, browser()->GetSessionID(), native.Get<0>(),
      native.Get<1>(), /*require_focus=*/true);
  EXPECT_EQ(ArcSplitVerification::kExact, receipt.verification);
  EXPECT_TRUE(receipt.focus_verified);
  EXPECT_EQ(pending.prepared->expected_native_structure_sha256,
            receipt.structure_sha256);
  EXPECT_EQ(64u, receipt.receipt_sha256.size());

  ArcImportPlan replay_plan;
  std::string replay_token;
  ASSERT_EQ(ArcImportStatus::kOk,
            ArcImportServiceTestPeer::SeedFromFiles(
                service_.get(), source_directory_.GetPath(), &replay_plan,
                &replay_token));
  EXPECT_EQ(token_, replay_token);
  EXPECT_EQ(plan_, replay_plan);
  base::test::TestFuture<ArcImportCommitResult> replay;
  service_->Commit(replay_token, ArcConflictResolution::kRename,
                   ConfirmedSelection(), browser(), replay.GetCallback());
  ASSERT_TRUE(replay.Wait());
  EXPECT_EQ(ArcImportStatus::kNoChanges, replay.Get().status);
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&live));
  EXPECT_EQ(durable, live);
  EXPECT_EQ(backups, BackupDirectories());
  EXPECT_EQ(initial_tab_count_ + 2, browser()->tab_strip_model()->count());
  for (size_t i = 0; i < members.size(); ++i) {
    ASSERT_TRUE(members[i]);
    EXPECT_EQ(members[i].get(), bridge_->FindTabByTreeNodeId(
                                    plan_.splits.front().member_node_ids[i]));
  }
  const auto replay_journal =
      ReadArcImportJournal(browser()->GetProfile()->GetPath());
  EXPECT_EQ(ArcImportJournalState::kCommitted, replay_journal.state);
  EXPECT_EQ(journal.committed, replay_journal.committed);
  EXPECT_TRUE(foreground->IsActive());
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       DestroyedPendingMemberCannotPublishCommittedReceipt) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const auto expected =
      MergeArcImportPlan(previous_tree_, plan_, ArcConflictResolution::kRename);
  ASSERT_EQ(ArcImportStatus::kOk, expected.status);
  ASSERT_TRUE(expected.merged_tree);
  base::test::TestFuture<ArcImportCommitResult> committed;
  service_->Commit(token_, ArcConflictResolution::kRename, ConfirmedSelection(),
                   browser(), committed.GetCallback());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return committed.IsReady() || (first_response_->has_received_request() &&
                                   second_response_->has_received_request());
  }));
  ASSERT_FALSE(committed.IsReady());
  auto* const member = bridge_->FindTabByTreeNodeId(
      plan_.splits.front().member_node_ids.front());
  ASSERT_TRUE(member);
  member->Close();
  ASSERT_TRUE(committed.Wait());
  EXPECT_EQ(ArcImportStatus::kRecoveryRequired, committed.Get().status);
  EXPECT_FALSE(service_->operation_in_progress());
  const auto journal = ReadArcImportJournal(browser()->GetProfile()->GetPath());
  ASSERT_EQ(ArcImportStatus::kOk, journal.status);
  ASSERT_EQ(ArcImportJournalState::kPrepared, journal.state);
  ASSERT_TRUE(journal.prepared);
  EXPECT_EQ(ArcImportPreparedPhase::kManualRecoveryRequired,
            journal.prepared->phase);
  EXPECT_TRUE(journal.prepared->native_receipt_sha256.empty());
  tab_tree::TabTreeSnapshot live;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&live));
  // Failure after native mutation must preserve both stores for recovery,
  // never compensate the imported tree while a surviving tab still exists.
  EXPECT_EQ(*expected.merged_tree, live);
  EXPECT_EQ(1u, BackupDirectories().size());
  EXPECT_EQ(initial_tab_count_ + 1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(bridge_->FindTabByTreeNodeId(
      plan_.splits.front().member_node_ids.back()));
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       UserEditDuringNavigationIsNeverSuppressedOrRolledBack) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TestFuture<ArcImportCommitResult> committed;
  service_->Commit(token_, ArcConflictResolution::kRename, ConfirmedSelection(),
                   browser(), committed.GetCallback());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return committed.IsReady() || (first_response_->has_received_request() &&
                                   second_response_->has_received_request());
  }));
  ASSERT_FALSE(committed.IsReady());
  const auto id = plan_.splits.front().member_node_ids.front();
  ASSERT_EQ(Store::Result::kOk,
            bridge_->tab_tree_store()->RenameNode(id, u"Keep my new title",
                                                  base::Time::Now()));
  first_response_->WaitForRequest();
  second_response_->WaitForRequest();
  first_response_->Send(
      net::HTTP_OK, "text/html",
      "<title>Imported first</title>" + std::string(2048, ' '));
  second_response_->Send(
      net::HTTP_OK, "text/html",
      "<title>Imported second</title>" + std::string(2048, ' '));
  ASSERT_TRUE(committed.Wait());
  EXPECT_EQ(ArcImportStatus::kRecoveryRequired, committed.Get().status);
  first_response_->Done();
  second_response_->Done();
  content::RunAllTasksUntilIdle();
  tab_tree::TreeNode node;
  ASSERT_EQ(Store::Result::kOk, bridge_->tab_tree_store()->GetNode(id, &node));
  EXPECT_EQ(u"Keep my new title", node.title);
  EXPECT_TRUE(bridge_->FindTabByTreeNodeId(id));
  const auto journal = ReadArcImportJournal(browser()->GetProfile()->GetPath());
  ASSERT_TRUE(journal.prepared);
  EXPECT_EQ(ArcImportPreparedPhase::kManualRecoveryRequired,
            journal.prepared->phase);
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       ExplicitRecoveryRestoresVerifiedBackupWithoutRetry) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  const auto backups = BackupDirectories();
  ASSERT_EQ(1u, backups.size());
  base::test::TestFuture<ArcImportPreview> result;
  service_->RecoverFailedImport(result.GetCallback());
  ASSERT_TRUE(result.Wait());
  EXPECT_EQ(ArcImportStatus::kOk, result.Get().status);
  EXPECT_TRUE(result.Get().snapshot_token.empty());
  EXPECT_FALSE(service_->operation_in_progress());
  tab_tree::TabTreeSnapshot restored;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&restored));
  EXPECT_EQ(previous_tree_, restored);
  EXPECT_EQ(ArcImportJournalState::kEmpty,
            ReadArcImportJournal(browser()->GetProfile()->GetPath()).state);
  EXPECT_EQ(backups, BackupDirectories());
  EXPECT_EQ(initial_tab_count_, browser()->tab_strip_model()->count());
  EXPECT_FALSE(first_response_->has_received_request());
  EXPECT_FALSE(second_response_->has_received_request());
  const auto copy = source_directory_.GetPath().AppendASCII("restored-copy");
  ASSERT_TRUE(base::CopyFile(
      browser()->GetProfile()->GetPath().AppendASCII(kTabTreeDatabaseFilename),
      copy));
  Store durable;
  ASSERT_TRUE(durable.Initialize(copy));
  ASSERT_EQ(Store::Result::kOk, durable.ExportSnapshot(&restored));
  EXPECT_EQ(previous_tree_, restored);
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       RecoveryPreservesNewerTreeEdits) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  ASSERT_EQ(Store::Result::kOk,
            bridge_->tab_tree_store()->RenameNode(
                plan_.splits.front().member_node_ids.front(), u"New user title",
                base::Time::Now()));
  ExpectRecoveryRejectedWithoutTreeOrJournalChange();
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       RecoveryPreservesNewTemporaryTabWorkspace) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  const auto id = plan_.tree.workspaces.front().id;
  ASSERT_TRUE(bridge_->SetActiveWorkspaceForWindow(
      browser(), id, WorkspaceActivationSource::kKeyboard));
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(tab);
  ASSERT_FALSE(bridge_->FindTreeNodeIdForTab(tab));
  ASSERT_EQ(id, bridge_->GetWorkspaceForTab(tab));
  tab_tree::TabTreeSnapshot unchanged;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&unchanged));
  ASSERT_EQ(prepared_.expected_tree_sha256,
            ComputeArcImportTreeFingerprint(unchanged));
  const auto tab_identity = tab->GetWeakPtr();
  ExpectRecoveryRejectedWithoutTreeOrJournalChange();
  ASSERT_TRUE(tab_identity);
  EXPECT_EQ(id, bridge_->GetWorkspaceForTab(tab_identity.get()));
  EXPECT_EQ(initial_tab_count_ + 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       RecoveryPreservesBoundNativeTab) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  const auto id = plan_.splits.front().member_node_ids.front();
  auto hold_metadata = bridge_->DeferSavedPageMetadataForNodes({id});
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(bridge_->RestoreTabSessionMetadata(
      tab,
      {.workspace_id = plan_.tree.workspaces.front().id, .tree_node_id = id}));
  ASSERT_EQ(tab, bridge_->FindTabByTreeNodeId(id));
  ExpectRecoveryRejectedWithoutTreeOrJournalChange();
  EXPECT_EQ(tab, bridge_->FindTabByTreeNodeId(id));
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       RecoveryRejectsChangedBackup) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  const auto backups = BackupDirectories();
  ASSERT_EQ(1u, backups.size());
  ASSERT_TRUE(
      base::AppendToFile(backups.front().AppendASCII("manifest.json"), "\n"));
  ExpectRecoveryRejectedWithoutTreeOrJournalChange();
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       RecoveryRejectsCompletedNativeReceipt) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_NO_FATAL_FAILURE(PrepareFailedTreeOnlyImport());
  prepared_.native_receipt_sha256 = std::string(64, 'a');
  ASSERT_TRUE(WriteArcImportPreparedJournal(browser()->GetProfile()->GetPath(),
                                            prepared_));
  ExpectRecoveryRejectedWithoutTreeOrJournalChange();
}

IN_PROC_BROWSER_TEST_F(ArcImportServiceBrowserTest,
                       ChangedSourceAndWrongTokenCannotMutateDestination) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TestFuture<ArcImportCommitResult> wrong_token;
  service_->Commit(token_ + "0", ArcConflictResolution::kRename,
                   ConfirmedSelection(), browser(), wrong_token.GetCallback());
  ASSERT_TRUE(wrong_token.Wait());
  EXPECT_EQ(ArcImportStatus::kStalePreview, wrong_token.Get().status);
  ASSERT_TRUE(base::AppendToFile(SidebarPath(), "\n"));
  base::test::TestFuture<ArcImportCommitResult> changed_source;
  service_->Commit(token_, ArcConflictResolution::kRename, ConfirmedSelection(),
                   browser(), changed_source.GetCallback());
  ASSERT_TRUE(changed_source.Wait());
  EXPECT_EQ(ArcImportStatus::kSourceChanged, changed_source.Get().status);
  tab_tree::TabTreeSnapshot unchanged;
  ASSERT_TRUE(bridge_->ExportTabTreeSnapshot(&unchanged));
  EXPECT_EQ(previous_tree_, unchanged);
  EXPECT_TRUE(BackupDirectories().empty());
  const auto journal = ReadArcImportJournal(browser()->GetProfile()->GetPath());
  EXPECT_EQ(ArcImportStatus::kOk, journal.status);
  EXPECT_EQ(ArcImportJournalState::kEmpty, journal.state);
  EXPECT_EQ(initial_tab_count_, browser()->tab_strip_model()->count());
  EXPECT_FALSE(first_response_->has_received_request());
  EXPECT_FALSE(second_response_->has_received_request());
}

}  // namespace
}  // namespace ahoi::importer::arc
