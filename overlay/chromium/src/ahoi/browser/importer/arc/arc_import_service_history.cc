// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <memory>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_service_internal.h"
#include "ahoi/browser/importer/arc/arc_import_service_factory.h"
#include "ahoi/browser/session/isolated_profile_registry.h"
#include "ahoi/browser/session/session_bridge.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/history/core/common/pref_names.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"

namespace ahoi::importer::arc {

namespace {

bool HistorySucceeded(ArcImportStatus status) {
  return status == ArcImportStatus::kOk || status == ArcImportStatus::kNoChanges;
}

}  // namespace

ArcHistoryImportRunner* ArcImportService::GetHistoryRunner() {
  if (!history_runner_ && profile_) {
    auto* service = HistoryServiceFactory::GetForProfile(
        profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (service) {
      history_runner_ = std::make_unique<ArcHistoryImportRunner>(service);
    }
  }
  return history_runner_.get();
}

bool ArcImportService::CanImportHistory() const {
  return profile_ && !profile_->IsOffTheRecord() && profile_->GetPrefs() &&
         !profile_->GetPrefs()->GetBoolean(prefs::kSavingBrowserHistoryDisabled);
}

void ArcImportService::RunHistoryOperation(
    std::optional<ArcHistoryImportRequest> request,
    ArcHistoryImportRunner::ImportCallback callback,
    bool within_operation) {
  if (!profile_ || profile_->IsOffTheRecord() || history_callback_ ||
      (operation_in_progress_ && !within_operation)) {
    std::move(callback).Run(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  // The user-triggered Settings operation may load an isolated Profile with
  // no open window. Keep that regular Profile alive until its terminal reply;
  // browser shutdown still cancels the runner before HistoryService teardown.
  history_keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
      profile_, ProfileKeepAliveOrigin::kChromeViewsDelegate);
  if (!history_keep_alive_) {
    std::move(callback).Run(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  history_owns_operation_ = !within_operation;
  operation_in_progress_ = true;
  history_callback_ = std::move(callback);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadArcHistoryJournal, profile_->GetPath()),
      base::BindOnce(&ArcImportService::OnHistoryJournalRead,
                     weak_factory_.GetWeakPtr(), std::move(request)));
}

void ArcImportService::OnHistoryJournalRead(
    std::optional<ArcHistoryImportRequest> request,
    ArcHistoryJournalReadResult journal) {
  if (journal.status != ArcImportStatus::kOk) {
    OnHistoryOperationFinished({.selected = true, .status = journal.status});
    return;
  }
  if (!journal.prepared && !request) {
    OnHistoryOperationFinished({.status = ArcImportStatus::kNoChanges});
    return;
  }
  auto* runner = CanImportHistory() ? GetHistoryRunner() : nullptr;
  if (!runner) {
    OnHistoryOperationFinished(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  if (journal.prepared) {
    runner->Recover(
        profile_->GetPath(), std::move(*journal.prepared),
        base::BindOnce(&ArcImportService::OnHistoryPreparedRecovered,
                       weak_factory_.GetWeakPtr(), std::move(request)));
    return;
  }
  StartHistoryImport(std::move(*request));
}

void ArcImportService::OnHistoryPreparedRecovered(
    std::optional<ArcHistoryImportRequest> request,
    ArcImportStatus status) {
  if (!HistorySucceeded(status) || !request) {
    OnHistoryOperationFinished({.selected = true, .status = status});
    return;
  }
  // Re-read the target policy after recovery, before starting a new batch.
  auto* runner = CanImportHistory() ? GetHistoryRunner() : nullptr;
  if (!runner) {
    OnHistoryOperationFinished(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  StartHistoryImport(std::move(*request));
}

void ArcImportService::StartHistoryImport(ArcHistoryImportRequest request) {
  if (!request.backup_identifier.empty()) {
    OnHistoryBackupFlushed(std::move(request), true);
    return;
  }
  if (!session_bridge_) {
    OnHistoryOperationFinished(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  // Fresh per-profile backups include that target's native tree database.
  // Serialize its pending writer before capturing the complete generation,
  // just as the sidebar backup does. Preserve the source/hash checks.
  session_bridge_->FlushPersistenceForBackup(base::BindOnce(
      &ArcImportService::OnHistoryBackupFlushed, weak_factory_.GetWeakPtr(),
      std::move(request)));
}

void ArcImportService::OnHistoryBackupFlushed(ArcHistoryImportRequest request,
                                            bool success) {
  auto* runner = success && CanImportHistory() ? GetHistoryRunner() : nullptr;
  if (!runner) {
    OnHistoryOperationFinished(
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  runner->Import(
      std::move(request),
      base::BindOnce(&ArcImportService::OnHistoryOperationFinished,
                     weak_factory_.GetWeakPtr()));
}

void ArcImportService::OnHistoryOperationFinished(ArcHistoryImportResult result) {
  auto callback = std::move(history_callback_);
  auto keep_alive = std::move(history_keep_alive_);
  if (history_owns_operation_) {
    operation_in_progress_ = false;
  }
  history_owns_operation_ = false;
  if (callback) {
    std::move(callback).Run(result);
  }
}

void ArcImportService::HistoryFinishCommit(
    std::unique_ptr<CommitContext> context) {
  if (!context->import_history) {
    operation_in_progress_ = false;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  context->result.history.selected = true;
  ImportNextHistoryProfile(std::move(context));
}

void ArcImportService::ImportNextHistoryProfile(
    std::unique_ptr<CommitContext> context) {
  if (!context->main_history_finished) {
    context->main_history_finished = true;
    ArcSource source = context->selected_source;
    std::erase_if(source.browser_profiles, [&context](const auto& profile) {
      return std::ranges::find(context->separated_arc_profiles,
                               profile.directory_name) !=
             context->separated_arc_profiles.end();
    });
    if (!source.browser_profiles.empty()) {
      ArcHistoryImportRequest request{.profile_path = profile_->GetPath(),
                                      .selected_source = std::move(source),
                                      .snapshot_token = context->snapshot_hash};
      // A backup containing separated sources must not be replayed into the
      // main Profile. Fresh, source-filtered backups bind each target's own
      // journal and recovery input without changing the sidebar journal.
      if (context->separated_arc_profiles.empty()) {
        request.backup_identifier = context->prepared.backup_identifier;
        request.manifest_sha256 = context->prepared.manifest_sha256;
      }
      RunHistoryOperation(
          std::move(request),
          base::BindOnce(&ArcImportService::OnHistoryProfileImported,
                         weak_factory_.GetWeakPtr(), std::move(context)),
          /*within_operation=*/true);
      return;
    }
  }
  if (context->history_profile_cursor >=
      context->separated_arc_profiles.size()) {
    const auto& history = context->result.history;
    if (!context->import_sidebar) {
      context->result.status = history.status == ArcImportStatus::kRecoveryRequired
                                   ? ArcImportStatus::kTransactionFailed
                                   : history.status;
    } else if (HistorySucceeded(history.status) && history.added_pages > 0 &&
               context->result.status == ArcImportStatus::kNoChanges) {
      context->result.status = ArcImportStatus::kOk;
    }
    // A committed sidebar remains authoritative even if a history target
    // failed; the separate history outcome reports partial completion.
    operation_in_progress_ = false;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  const std::string arc_profile =
      context->separated_arc_profiles[context->history_profile_cursor++];
  auto* manager = g_browser_process ? g_browser_process->profile_manager() : nullptr;
  auto entry = session::FindIsolatedProfileByWorkspaceId(
      g_browser_process ? g_browser_process->local_state() : nullptr,
      ArcSeparatedWorkspaceId(arc_profile));
  if (!manager || g_browser_process->IsShuttingDown() || !entry ||
      entry->state != session::IsolatedProfileState::kActive) {
    OnHistoryProfileImported(
        std::move(context),
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  const auto target_path = manager->user_data_dir().AppendASCII(entry->profile_dir);
  // M155 calls the callback with nullptr even when this returns false.
  // Never create an arbitrary Profile or fall back to the main service.
  manager->LoadProfileByPath(
      target_path, /*incognito=*/false,
      base::BindOnce(&ArcImportService::OnHistoryTargetLoaded,
                     weak_factory_.GetWeakPtr(), std::move(context), arc_profile,
                     target_path));
}

void ArcImportService::OnHistoryTargetLoaded(
    std::unique_ptr<CommitContext> context,
    std::string arc_profile,
    base::FilePath target_path,
    Profile* target) {
  const auto entry = session::FindIsolatedProfileByWorkspaceId(
      g_browser_process ? g_browser_process->local_state() : nullptr,
      ArcSeparatedWorkspaceId(arc_profile));
  auto* service = target && target->GetPath() == target_path &&
                          !target->IsOffTheRecord() && entry &&
                          entry->profile_dir == target_path.BaseName().MaybeAsASCII() &&
                          entry->state == session::IsolatedProfileState::kActive
                      ? ArcImportServiceFactory::GetForProfile(target)
                      : nullptr;
  if (!service || service == this) {
    OnHistoryProfileImported(
        std::move(context),
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  ArcSource source = context->selected_source;
  std::erase_if(source.browser_profiles, [&arc_profile](const auto& profile) {
    return profile.directory_name != arc_profile;
  });
  if (source.browser_profiles.size() != 1) {
    OnHistoryProfileImported(
        std::move(context),
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  ArcHistoryImportRequest request{.profile_path = target_path,
                                  .selected_source = std::move(source),
                                  .snapshot_token = context->snapshot_hash};
  service->RunHistoryOperation(
      std::move(request),
      base::BindOnce(&ArcImportService::OnHistoryProfileImported,
                     weak_factory_.GetWeakPtr(), std::move(context)),
      /*within_operation=*/false);
}

void ArcImportService::OnHistoryProfileImported(
    std::unique_ptr<CommitContext> context,
    ArcHistoryImportResult result) {
  auto& aggregate = context->result.history;
  aggregate.added_pages += result.added_pages;
  aggregate.deduplicated_pages += result.deduplicated_pages;
  aggregate.expired_pages += result.expired_pages;
  aggregate.excluded_pages += result.excluded_pages;
  if (HistorySucceeded(aggregate.status)) {
    if (!HistorySucceeded(result.status) || result.status == ArcImportStatus::kOk) {
      aggregate.status = result.status;
    }
  }
  ImportNextHistoryProfile(std::move(context));
}

void ArcImportService::RecoverHistoryBeforePreview(
    uint64_t generation,
    ArcImportPreviewCallback callback,
    DiscoveryResult result) {
  if (!result.history_recovery_started) {
    result.history_recovery_started = true;
    result.history_available = CanImportHistory();
    RunHistoryOperation(
        std::nullopt,
        base::BindOnce(&ArcImportService::OnPreviewHistoryRecovered,
                       weak_factory_.GetWeakPtr(), generation,
                       std::move(callback), std::move(result)),
        /*within_operation=*/true);
    return;
  }
  if (result.plan) {
    auto* manager = g_browser_process ? g_browser_process->profile_manager() : nullptr;
    while (result.history_recovery_cursor < result.plan->arc_profiles.size()) {
      const auto& arc_profile =
          result.plan->arc_profiles[result.history_recovery_cursor++];
      const auto entry = session::FindIsolatedProfileByWorkspaceId(
          g_browser_process ? g_browser_process->local_state() : nullptr,
          ArcSeparatedWorkspaceId(arc_profile.directory_name));
      if (!entry || entry->state != session::IsolatedProfileState::kActive) {
        continue;
      }
      if (!manager) {
        result.status = ArcImportStatus::kTransactionFailed;
        break;
      }
      const auto path = manager->user_data_dir().AppendASCII(entry->profile_dir);
      if (path == profile_->GetPath()) {
        continue;
      }
      manager->LoadProfileByPath(
          path, /*incognito=*/false,
          base::BindOnce(&ArcImportService::OnPreviewHistoryTargetLoaded,
                         weak_factory_.GetWeakPtr(), generation,
                         std::move(callback), std::move(result), path));
      return;
    }
  }
  OnDiscoveryComplete(generation, std::move(callback), std::move(result));
}

void ArcImportService::OnPreviewHistoryTargetLoaded(
    uint64_t generation,
    ArcImportPreviewCallback callback,
    DiscoveryResult result,
    base::FilePath target_path,
    Profile* target) {
  const auto entry = session::FindIsolatedProfile(
      g_browser_process ? g_browser_process->local_state() : nullptr,
      target_path.BaseName().MaybeAsASCII());
  auto* service = target && !target->IsOffTheRecord() &&
                          target->GetPath() == target_path && entry &&
                          entry->state == session::IsolatedProfileState::kActive &&
                          result.plan && result.history_recovery_cursor > 0 &&
                          entry->workspace_id == ArcSeparatedWorkspaceId(
                              result.plan->arc_profiles[
                                  result.history_recovery_cursor - 1].directory_name)
                      ? ArcImportServiceFactory::GetForProfile(target)
                      : nullptr;
  if (!service || service == this) {
    OnPreviewHistoryRecovered(
        generation, std::move(callback), std::move(result),
        {.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  result.history_available |= service->CanImportHistory();
  service->RunHistoryOperation(
      std::nullopt,
      base::BindOnce(&ArcImportService::OnPreviewHistoryRecovered,
                     weak_factory_.GetWeakPtr(), generation, std::move(callback),
                     std::move(result)),
      /*within_operation=*/false);
}

void ArcImportService::OnPreviewHistoryRecovered(
    uint64_t generation,
    ArcImportPreviewCallback callback,
    DiscoveryResult result,
    ArcHistoryImportResult history) {
  if (history.status != ArcImportStatus::kOk &&
      history.status != ArcImportStatus::kNoChanges) {
    // History's prepared recovery is automatic; the sidebar's manual recovery
    // action must not be offered for an unrelated history journal.
    result.status = history.status == ArcImportStatus::kRecoveryRequired
                        ? ArcImportStatus::kTransactionFailed
                        : history.status;
    operation_in_progress_ = false;
    std::move(callback).Run({.status = result.status});
    return;
  }
  RecoverHistoryBeforePreview(generation, std::move(callback), std::move(result));
}

}  // namespace ahoi::importer::arc
