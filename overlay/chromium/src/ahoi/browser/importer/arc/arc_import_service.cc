// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_service.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_backup.h"
#include "ahoi/browser/importer/arc/arc_import_discovery.h"
#include "ahoi/browser/importer/arc/arc_import_parser.h"
#include "ahoi/browser/importer/arc/arc_import_snapshot.h"
#include "ahoi/browser/importer/arc/arc_split_runtime.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/tab_tree/tab_tree_store.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

namespace ahoi::importer::arc {

namespace {

constexpr int kJournalSchemaVersion = 1;
constexpr int64_t kMaxJournalBytes = 64 * 1024;
constexpr base::FilePath::CharType kJournalDirectory[] =
    FILE_PATH_LITERAL("Ahoi");
constexpr base::FilePath::CharType kJournalFilename[] =
    FILE_PATH_LITERAL("ArcImportJournal.json");
constexpr base::FilePath::CharType kJournalTemporaryFilename[] =
    FILE_PATH_LITERAL("ArcImportJournal.json.tmp");

struct JournalReadResult {
  ArcImportStatus status = ArcImportStatus::kOk;
  std::string committed_snapshot_hash;
};

base::FilePath JournalPath(const base::FilePath& profile_path) {
  return profile_path.Append(kJournalDirectory).Append(kJournalFilename);
}

JournalReadResult ReadJournal(const base::FilePath& profile_path) {
  const base::FilePath path = JournalPath(profile_path);
  if (!base::PathExists(path)) {
    return {};
  }
  if (base::IsLink(path)) {
    return {.status = ArcImportStatus::kJournalError};
  }
  base::File::Info info;
  if (!base::GetFileInfo(path, &info) || info.is_directory ||
      info.is_symbolic_link || info.size < 0 || info.size > kMaxJournalBytes) {
    return {.status = ArcImportStatus::kJournalError};
  }
  std::string json;
  if (!base::ReadFileToStringWithMaxSize(path, &json, kMaxJournalBytes)) {
    return {.status = ArcImportStatus::kJournalError};
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(json);
  const base::Value::Dict* dict =
      parsed.has_value() ? parsed->GetIfDict() : nullptr;
  const std::optional<int> version =
      dict ? dict->FindInt("version") : std::nullopt;
  const std::string* status = dict ? dict->FindString("status") : nullptr;
  const std::string* hash =
      dict ? dict->FindString("snapshot_sha256") : nullptr;
  if (!version.has_value() || *version != kJournalSchemaVersion || !status ||
      *status != "committed" || !hash || hash->size() != 64 ||
      !std::ranges::all_of(*hash, [](char value) {
        return base::IsHexDigit(value) &&
               (base::IsAsciiDigit(value) || base::IsAsciiLower(value));
      })) {
    return {.status = ArcImportStatus::kJournalError};
  }
  return {.committed_snapshot_hash = *hash};
}

bool WriteJournal(const base::FilePath& profile_path,
                  const std::string& snapshot_hash,
                  const ArcImportCommitResult& result) {
  if (snapshot_hash.size() != 64 ||
      !std::ranges::all_of(snapshot_hash, [](char value) {
        return base::IsHexDigit(value) &&
               (base::IsAsciiDigit(value) || base::IsAsciiLower(value));
      })) {
    return false;
  }
  const base::FilePath path = JournalPath(profile_path);
  const base::FilePath directory = path.DirName();
  if (base::IsLink(directory) || base::IsLink(path) ||
      (!base::DirectoryExists(directory) &&
       !base::CreateDirectory(directory)) ||
      !base::SetPosixFilePermissions(directory, 0700)) {
    return false;
  }

  base::Value::Dict journal;
  journal.Set("version", kJournalSchemaVersion);
  journal.Set("snapshot_sha256", snapshot_hash);
  journal.Set("status", "committed");
  journal.Set("workspaces",
              static_cast<int>(result.stats.imported_workspace_count));
  journal.Set("folders", static_cast<int>(result.stats.imported_folder_count));
  journal.Set("pages", static_cast<int>(result.stats.imported_page_count));
  journal.Set("splits", static_cast<int>(result.stats.imported_split_count));
  journal.Set("degraded_splits",
              static_cast<int>(result.stats.degraded_split_count));
  journal.Set("renamed_workspaces",
              static_cast<int>(result.renamed_workspace_count));
  journal.Set("skipped_workspaces",
              static_cast<int>(result.skipped_workspace_count));
  journal.Set("merged_workspaces",
              static_cast<int>(result.merged_workspace_count));
  journal.Set("reconstructed_splits",
              static_cast<int>(result.reconstructed_split_count));
  std::string json;
  if (!base::JSONWriter::Write(journal, &json)) {
    return false;
  }

  const base::FilePath temporary = directory.Append(kJournalTemporaryFilename);
  if (base::IsLink(temporary) ||
      (base::PathExists(temporary) && !base::DeleteFile(temporary))) {
    return false;
  }
  bool success = base::WriteFile(temporary, json) &&
                 base::SetPosixFilePermissions(temporary, 0600);
  base::File::Error replace_error = base::File::FILE_OK;
  if (success) {
    success = base::ReplaceFile(temporary, path, &replace_error) &&
              base::SetPosixFilePermissions(path, 0600);
  }
  if (!success) {
    base::DeleteFile(temporary);
  }
  return success;
}

std::string SnapshotToken(const ArcImportSnapshot& snapshot) {
  return base::HexEncodeLower(snapshot.sha256);
}

ArcImportStatus ValidateCommitSource(const ArcSource& source,
                                     const std::string& expected_token) {
  if (IsArcApplicationRunning() || AreArcProfileFilesOpen(source)) {
    return ArcImportStatus::kSourceInUse;
  }
  ArcSnapshotResult snapshot = CaptureArcSnapshot(source);
  if (snapshot.status != ArcImportStatus::kOk || !snapshot.snapshot) {
    return snapshot.status;
  }
  return SnapshotToken(*snapshot.snapshot) == expected_token
             ? ArcImportStatus::kOk
             : ArcImportStatus::kSourceChanged;
}

bool IsValidSelection(const ArcImportSelection& selection,
                      const ArcSource& source) {
  if (!selection.import_sidebar || !selection.backup_confirmed ||
      !selection.commit_confirmed ||
      selection.selected_browser_profiles.empty()) {
    return false;
  }
  std::set<std::string> available;
  for (const ArcBrowserProfile& profile : source.browser_profiles) {
    available.insert(profile.directory_name);
  }
  std::set<std::string> selected;
  for (const std::string& profile : selection.selected_browser_profiles) {
    if (!available.contains(profile) || !selected.insert(profile).second) {
      return false;
    }
  }
  return true;
}

ArcSource SelectBrowserProfiles(const ArcSource& source,
                                const ArcImportSelection& selection) {
  ArcSource selected = source;
  std::erase_if(selected.browser_profiles,
                [&selection](const ArcBrowserProfile& profile) {
                  return std::ranges::find(selection.selected_browser_profiles,
                                           profile.directory_name) ==
                         selection.selected_browser_profiles.end();
                });
  return selected;
}

ArcImportPlan SelectImportCategories(const ArcImportPlan& plan,
                                     const ArcImportSelection& selection) {
  ArcImportPlan selected = plan;
  if (!selection.reconstruct_splits) {
    selected.stats.degraded_split_count += selected.stats.imported_split_count;
    selected.stats.imported_split_count = 0;
    selected.splits.clear();
  }
  return selected;
}

}  // namespace

struct ArcImportService::DiscoveryResult {
  ArcImportStatus status = ArcImportStatus::kNotFound;
  std::optional<ArcImportPlan> plan;
  std::string snapshot_token;
  std::string committed_snapshot_hash;
  std::optional<ArcSource> source;
  bool arc_is_running = false;
};

struct ArcImportService::CommitContext {
  ArcImportCommitCallback callback;
  ArcImportCommitResult result;
  tab_tree::TabTreeSnapshot previous_tree;
  std::vector<base::WeakPtr<tabs::TabInterface>> opened_tabs;
  std::string snapshot_hash;
};

ArcImportService::DiscoveryResult ArcImportService::DiscoverImport(
    const base::FilePath& profile_path) {
  ArcImportService::DiscoveryResult result;
  const JournalReadResult journal = ReadJournal(profile_path);
  if (journal.status != ArcImportStatus::kOk) {
    result.status = journal.status;
    return result;
  }
  result.committed_snapshot_hash = journal.committed_snapshot_hash;

  if (IsArcApplicationRunning()) {
    result.status = ArcImportStatus::kSourceInUse;
    result.arc_is_running = true;
    return result;
  }

  const ArcDiscoveryResult discovery = DiscoverDefaultArcSource();
  if (discovery.status != ArcImportStatus::kOk || !discovery.source) {
    result.status = discovery.status;
    return result;
  }
  if (AreArcProfileFilesOpen(*discovery.source)) {
    result.status = ArcImportStatus::kSourceInUse;
    result.arc_is_running = true;
    return result;
  }
  ArcSnapshotResult snapshot = CaptureArcSnapshot(*discovery.source);
  if (snapshot.status != ArcImportStatus::kOk || !snapshot.snapshot) {
    result.status = snapshot.status;
    return result;
  }
  result.snapshot_token = SnapshotToken(*snapshot.snapshot);
  ArcParseResult parsed = ParseArcSnapshot(*snapshot.snapshot);
  if (parsed.status != ArcImportStatus::kOk || !parsed.plan) {
    result.status = parsed.status;
    return result;
  }
  result.status = ArcImportStatus::kOk;
  result.source = discovery.source;
  result.plan = std::move(parsed.plan);
  return result;
}

ArcImportService::ArcImportService(Profile* profile,
                                   SessionBridge* session_bridge)
    : profile_(profile), session_bridge_(session_bridge) {}

ArcImportService::~ArcImportService() = default;

void ArcImportService::Shutdown() {
  ++discovery_generation_;
  operation_in_progress_ = false;
  pending_plan_.reset();
  pending_source_.reset();
  pending_snapshot_token_.clear();
  weak_factory_.InvalidateWeakPtrs();
  session_bridge_ = nullptr;
  profile_ = nullptr;
}

void ArcImportService::DiscoverAndPreview(ArcImportPreviewCallback callback) {
  if (!callback) {
    return;
  }
  if (!profile_ || !session_bridge_ || operation_in_progress_) {
    std::move(callback).Run({.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  operation_in_progress_ = true;
  pending_plan_.reset();
  pending_source_.reset();
  pending_snapshot_token_.clear();
  const uint64_t generation = ++discovery_generation_;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ArcImportService::DiscoverImport, profile_->GetPath()),
      base::BindOnce(&ArcImportService::OnDiscoveryComplete,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

void ArcImportService::OnDiscoveryComplete(uint64_t generation,
                                           ArcImportPreviewCallback callback,
                                           DiscoveryResult result) {
  if (generation != discovery_generation_) {
    return;
  }
  operation_in_progress_ = false;
  ArcImportPreview preview{.status = result.status};
  preview.arc_is_running = result.arc_is_running;
  if (result.status != ArcImportStatus::kOk || !result.plan || !result.source ||
      !session_bridge_ || !session_bridge_->is_ready()) {
    if (result.status == ArcImportStatus::kOk) {
      preview.status = ArcImportStatus::kTransactionFailed;
    }
    std::move(callback).Run(std::move(preview));
    return;
  }

  tab_tree::TabTreeSnapshot current;
  if (!session_bridge_->ExportTabTreeSnapshot(&current)) {
    preview.status = ArcImportStatus::kTransactionFailed;
    std::move(callback).Run(std::move(preview));
    return;
  }
  std::set<std::u16string> existing_names;
  std::set<base::Uuid> existing_ids;
  for (const tab_tree::Workspace& workspace : current.workspaces) {
    if (!workspace.tombstone) {
      existing_names.insert(workspace.name);
      existing_ids.insert(workspace.id);
    }
  }
  for (const tab_tree::Workspace& workspace : result.plan->tree.workspaces) {
    preview.target_workspace_names.push_back(workspace.name);
    if (!existing_ids.contains(workspace.id) &&
        existing_names.contains(workspace.name)) {
      ++preview.conflicting_workspace_count;
    }
  }
  for (const ArcBrowserProfile& profile : result.source->browser_profiles) {
    preview.available_browser_profiles.push_back(profile.directory_name);
  }
  const ArcImportMergeResult idempotence =
      MergeArcImportPlan(current, *result.plan, ArcConflictResolution::kRename);
  preview.already_imported =
      result.committed_snapshot_hash == result.snapshot_token &&
      idempotence.status == ArcImportStatus::kNoChanges;
  preview.snapshot_token = result.snapshot_token;
  preview.stats = result.plan->stats;
  committed_snapshot_hash_ = std::move(result.committed_snapshot_hash);
  pending_snapshot_token_ = result.snapshot_token;
  pending_source_ = std::move(result.source);
  pending_plan_ = std::move(result.plan);
  std::move(callback).Run(std::move(preview));
}

void ArcImportService::Commit(std::string snapshot_token,
                              ArcConflictResolution conflict_resolution,
                              ArcImportSelection selection,
                              BrowserWindowInterface* browser,
                              ArcImportCommitCallback callback) {
  ArcImportCommitResult result;
  if (!callback) {
    return;
  }
  if (!profile_ || !session_bridge_ || !session_bridge_->is_ready() ||
      operation_in_progress_ || !browser || !pending_plan_ ||
      !pending_source_ || browser->GetProfile() != profile_ ||
      !IsValidSelection(selection, *pending_source_) ||
      snapshot_token.empty() || snapshot_token != pending_snapshot_token_) {
    result.status = snapshot_token == pending_snapshot_token_
                        ? ArcImportStatus::kTransactionFailed
                        : ArcImportStatus::kStalePreview;
    std::move(callback).Run(std::move(result));
    return;
  }

  operation_in_progress_ = true;
  const std::string validation_token = snapshot_token;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ValidateCommitSource, *pending_source_, validation_token),
      base::BindOnce(&ArcImportService::OnCommitSourceValidated,
                     weak_factory_.GetWeakPtr(), std::move(snapshot_token),
                     conflict_resolution, std::move(selection),
                     browser->GetWeakPtr(), std::move(callback)));
}

void ArcImportService::OnCommitSourceValidated(
    std::string snapshot_token,
    ArcConflictResolution conflict_resolution,
    ArcImportSelection selection,
    base::WeakPtr<BrowserWindowInterface> browser,
    ArcImportCommitCallback callback,
    ArcImportStatus validation_status) {
  ArcImportCommitResult result;
  if (validation_status != ArcImportStatus::kOk || !profile_ ||
      !session_bridge_ || !session_bridge_->is_ready() || !browser ||
      browser->GetProfile() != profile_ || !pending_plan_ || !pending_source_ ||
      !IsValidSelection(selection, *pending_source_) ||
      snapshot_token != pending_snapshot_token_) {
    operation_in_progress_ = false;
    result.status = validation_status == ArcImportStatus::kOk
                        ? ArcImportStatus::kStalePreview
                        : validation_status;
    std::move(callback).Run(std::move(result));
    return;
  }

  const ArcImportPlan selected_plan =
      SelectImportCategories(*pending_plan_, selection);
  tab_tree::TabTreeSnapshot current;
  if (!session_bridge_->ExportTabTreeSnapshot(&current)) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  const ArcImportMergeResult preflight =
      MergeArcImportPlan(current, selected_plan, conflict_resolution);
  result.status = preflight.status;
  result.stats = selected_plan.stats;
  result.renamed_workspace_count = preflight.renamed_workspace_count;
  result.skipped_workspace_count = preflight.skipped_workspace_count;
  result.merged_workspace_count = preflight.merged_workspace_count;
  if (preflight.status != ArcImportStatus::kOk &&
      preflight.status != ArcImportStatus::kNoChanges) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  if (preflight.status == ArcImportStatus::kNoChanges &&
      committed_snapshot_hash_ == snapshot_token) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }

  session_bridge_->FlushPersistenceForBackup(
      base::BindOnce(&ArcImportService::OnPersistenceFlushedBeforeBackup,
                     weak_factory_.GetWeakPtr(), std::move(snapshot_token),
                     conflict_resolution, std::move(selection),
                     std::move(browser), std::move(callback)));
}

void ArcImportService::OnPersistenceFlushedBeforeBackup(
    std::string snapshot_token,
    ArcConflictResolution conflict_resolution,
    ArcImportSelection selection,
    base::WeakPtr<BrowserWindowInterface> browser,
    ArcImportCommitCallback callback,
    bool persistence_flushed) {
  if (!persistence_flushed || !profile_ || !pending_source_ ||
      snapshot_token != pending_snapshot_token_ ||
      !IsValidSelection(selection, *pending_source_)) {
    operation_in_progress_ = false;
    ArcImportCommitResult result;
    result.status = persistence_flushed ? ArcImportStatus::kStalePreview
                                        : ArcImportStatus::kBackupError;
    std::move(callback).Run(std::move(result));
    return;
  }
  ArcSource selected_source =
      SelectBrowserProfiles(*pending_source_, selection);
  const std::string backup_token = snapshot_token;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath profile_path, ArcSource source, std::string token) {
            return CreateArcImportBackup(profile_path, source, token).status;
          },
          profile_->GetPath(), std::move(selected_source), backup_token),
      base::BindOnce(&ArcImportService::OnBackupComplete,
                     weak_factory_.GetWeakPtr(), std::move(snapshot_token),
                     conflict_resolution, std::move(selection),
                     std::move(browser), std::move(callback)));
}

void ArcImportService::OnBackupComplete(
    std::string snapshot_token,
    ArcConflictResolution conflict_resolution,
    ArcImportSelection selection,
    base::WeakPtr<BrowserWindowInterface> browser,
    ArcImportCommitCallback callback,
    ArcImportStatus backup_status) {
  ArcImportCommitResult result;
  if (backup_status != ArcImportStatus::kOk || !profile_ || !session_bridge_ ||
      !session_bridge_->is_ready() || !browser ||
      browser->GetProfile() != profile_ || !pending_plan_ || !pending_source_ ||
      !IsValidSelection(selection, *pending_source_) ||
      snapshot_token != pending_snapshot_token_) {
    operation_in_progress_ = false;
    result.status = backup_status == ArcImportStatus::kOk
                        ? ArcImportStatus::kStalePreview
                        : backup_status;
    std::move(callback).Run(std::move(result));
    return;
  }

  const ArcImportPlan selected_plan =
      SelectImportCategories(*pending_plan_, selection);
  tab_tree::TabTreeSnapshot previous;
  if (!session_bridge_->ExportTabTreeSnapshot(&previous)) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  ArcImportMergeResult merge =
      MergeArcImportPlan(previous, selected_plan, conflict_resolution);
  result.status = merge.status;
  result.stats = selected_plan.stats;
  result.renamed_workspace_count = merge.renamed_workspace_count;
  result.skipped_workspace_count = merge.skipped_workspace_count;
  result.merged_workspace_count = merge.merged_workspace_count;
  if (merge.status != ArcImportStatus::kOk &&
      merge.status != ArcImportStatus::kNoChanges) {
    operation_in_progress_ = false;
    std::move(callback).Run(std::move(result));
    return;
  }

  if (merge.status == ArcImportStatus::kNoChanges &&
      selected_plan.splits.empty()) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&WriteJournal, profile_->GetPath(), snapshot_token,
                       result),
        base::BindOnce(&ArcImportService::FinishNoChangeJournalWrite,
                       weak_factory_.GetWeakPtr(), std::move(callback), result,
                       snapshot_token));
    return;
  }
  if (!merge.merged_tree || !merge.applied_plan) {
    operation_in_progress_ = false;
    result.status = ArcImportStatus::kTransactionFailed;
    std::move(callback).Run(std::move(result));
    return;
  }

  if (merge.status == ArcImportStatus::kOk &&
      session_bridge_->ApplySyncedTabTreeSnapshot(std::move(
          *merge.merged_tree)) != tab_tree::TabTreeStore::Result::kOk) {
    // ApplySyncedTabTreeSnapshot can report a post-commit refresh failure
    // after its SQLite transaction succeeded. Always restore the exact
    // pre-import snapshot before reporting failure.
    auto context = std::make_unique<CommitContext>();
    context->callback = std::move(callback);
    context->result = result;
    context->previous_tree = std::move(previous);
    context->snapshot_hash = snapshot_token;
    RollbackAndFinish(std::move(context), ArcImportStatus::kTransactionFailed);
    return;
  }

  // A no-change tree with no skipped workspace and a missing journal is the
  // only crash-recovery case: the durable SQLite transaction landed before
  // the final journal rename. Rebuild native splits from deterministic
  // existing nodes, then finish the journal. A skip result must instead use
  // its empty applied plan and therefore never materialize skipped tabs.
  const bool recover_existing_snapshot =
      merge.status == ArcImportStatus::kNoChanges &&
      merge.skipped_workspace_count == 0;
  const ArcImportPlan& runtime_plan =
      recover_existing_snapshot ? selected_plan : *merge.applied_plan;
  ArcSplitRuntimeResult runtime =
      ReconstructArcSplits(browser.get(), session_bridge_, runtime_plan);
  if (runtime.status != ArcImportStatus::kOk) {
    auto context = std::make_unique<CommitContext>();
    context->callback = std::move(callback);
    context->result = result;
    context->previous_tree = std::move(previous);
    context->opened_tabs = std::move(runtime.opened_tabs);
    context->snapshot_hash = snapshot_token;
    RollbackAndFinish(std::move(context), ArcImportStatus::kRuntimeFailed);
    return;
  }
  result.reconstructed_split_count = runtime.reconstructed_split_count;
  result.approximated_four_pane_ratio_count =
      runtime.approximated_four_pane_ratio_count;

  auto context = std::make_unique<CommitContext>();
  context->callback = std::move(callback);
  context->result = result;
  context->previous_tree = std::move(previous);
  context->opened_tabs = std::move(runtime.opened_tabs);
  context->snapshot_hash = snapshot_token;
  session_bridge_->FlushPersistenceForBackup(
      base::BindOnce(&ArcImportService::OnCommittedPersistenceFlushed,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnCommittedPersistenceFlushed(
    std::unique_ptr<CommitContext> context,
    bool persistence_flushed) {
  if (!persistence_flushed || !profile_) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kTransactionFailed);
    return;
  }
  const base::FilePath profile_path = profile_->GetPath();
  const std::string snapshot_hash = context->snapshot_hash;
  const ArcImportCommitResult journal_result = context->result;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteJournal, profile_path, snapshot_hash,
                     journal_result),
      base::BindOnce(&ArcImportService::FinishJournalWrite,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::FinishJournalWrite(
    std::unique_ptr<CommitContext> context,
    bool journal_written) {
  if (!journal_written || !session_bridge_) {
    RollbackAndFinish(std::move(context), ArcImportStatus::kJournalError);
    return;
  }
  operation_in_progress_ = false;
  context->result.status = ArcImportStatus::kOk;
  committed_snapshot_hash_ = context->snapshot_hash;
  std::move(context->callback).Run(std::move(context->result));
}

void ArcImportService::RollbackAndFinish(std::unique_ptr<CommitContext> context,
                                         ArcImportStatus failure_status) {
  if (!session_bridge_ || session_bridge_->ApplySyncedTabTreeSnapshot(
                              std::move(context->previous_tree)) !=
                              tab_tree::TabTreeStore::Result::kOk) {
    operation_in_progress_ = false;
    CloseArcImportRuntimeTabs(std::move(context->opened_tabs));
    context->result.status = ArcImportStatus::kTransactionFailed;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  CloseArcImportRuntimeTabs(std::move(context->opened_tabs));
  session_bridge_->FlushPersistenceForBackup(base::BindOnce(
      &ArcImportService::FinishRollback, weak_factory_.GetWeakPtr(),
      std::move(context), failure_status));
}

void ArcImportService::FinishRollback(std::unique_ptr<CommitContext> context,
                                      ArcImportStatus failure_status,
                                      bool persistence_flushed) {
  operation_in_progress_ = false;
  context->result.status = persistence_flushed
                               ? failure_status
                               : ArcImportStatus::kTransactionFailed;
  std::move(context->callback).Run(std::move(context->result));
}

void ArcImportService::FinishNoChangeJournalWrite(
    ArcImportCommitCallback callback,
    ArcImportCommitResult result,
    std::string snapshot_hash,
    bool journal_written) {
  operation_in_progress_ = false;
  if (!journal_written) {
    result.status = ArcImportStatus::kJournalError;
  } else {
    result.status = ArcImportStatus::kNoChanges;
    committed_snapshot_hash_ = std::move(snapshot_hash);
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace ahoi::importer::arc
