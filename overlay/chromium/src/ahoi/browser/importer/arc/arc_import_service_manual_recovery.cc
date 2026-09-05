// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_recovery.h"
#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_service_internal.h"
#include "ahoi/browser/importer/arc/arc_import_tree_fingerprint.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/workspace_session_metadata.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"

namespace ahoi::importer::arc {

bool ArcImportService::HasAffectedLiveTabs(
    const ArcImportPreparedState& prepared,
    const std::vector<base::Uuid>& removed_workspaces) const {
  if (!profile_ || !session_bridge_ || !session_bridge_->is_ready() ||
      SessionRestore::IsRestoring(profile_) || prepared.affected_ids.empty()) {
    return true;
  }
  for (const auto& value : prepared.affected_ids) {
    const base::Uuid id = base::Uuid::ParseLowercase(value);
    if (!id.is_valid() || session_bridge_->FindTabByTreeNodeId(id)) {
      return true;
    }
  }
  for (const auto& id : removed_workspaces) {
    if (session_bridge_->HasLiveTabsInWorkspace(id)) {
      return true;
    }
  }
  return false;
}

void ArcImportService::RecoverFailedImport(ArcImportPreviewCallback callback) {
  if (!callback) {
    return;
  }
  if (operation_in_progress_ || !profile_ || !session_bridge_ ||
      !session_bridge_->is_ready()) {
    std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
    return;
  }
  operation_in_progress_ = true;
  ++discovery_generation_;
  pending_plan_.reset();
  pending_source_.reset();
  pending_snapshot_token_.clear();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadArcImportJournal, profile_->GetPath()),
      base::BindOnce(&ArcImportService::OnManualRecoveryJournalRead,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ArcImportService::OnManualRecoveryJournalRead(
    ArcImportPreviewCallback callback,
    ArcImportJournalReadResult journal) {
  if (journal.status != ArcImportStatus::kOk || !journal.prepared ||
      journal.state != ArcImportJournalState::kPrepared || !profile_ ||
      journal.prepared->phase !=
          ArcImportPreparedPhase::kManualRecoveryRequired ||
      !journal.prepared->native_receipt_sha256.empty() ||
      HasAffectedLiveTabs(*journal.prepared)) {
    FinishManualRecoveryRejected(std::move(callback));
    return;
  }
  auto context = std::make_unique<ManualRecoveryContext>();
  context->callback = std::move(callback);
  context->prepared = std::move(*journal.prepared);
  if (!session_bridge_->ExportTabTreeSnapshot(&context->start_tree)) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  auto task = base::BindOnce(&VerifyAndLoadArcImportBackup, profile_->GetPath(),
                             context->prepared.backup_identifier,
                             context->prepared.manifest_sha256,
                             context->prepared.snapshot_hash);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(task),
      base::BindOnce(&ArcImportService::OnManualRecoveryBackupLoaded,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnManualRecoveryBackupLoaded(
    std::unique_ptr<ManualRecoveryContext> context,
    ArcImportBackupRecoveryResult backup) {
  tab_tree::TabTreeSnapshot live;
  if (backup.status != ArcImportStatus::kOk || !backup.previous_tree ||
      HasAffectedLiveTabs(context->prepared) ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != context->start_tree) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  context->previous_tree = std::move(*backup.previous_tree);
  for (const auto& workspace : context->start_tree.workspaces) {
    if (std::ranges::none_of(
            context->previous_tree.workspaces,
            [&](const auto& old) { return old.id == workspace.id; })) {
      context->removed_workspaces.push_back(workspace.id);
    }
  }
  auto task = base::BindOnce(
      [](base::FilePath path, ArcImportPreparedState prepared,
         tab_tree::TabTreeSnapshot before, tab_tree::TabTreeSnapshot current) {
        const auto journal = ReadArcImportJournal(path);
        if (journal.status != ArcImportStatus::kOk || !journal.prepared ||
            *journal.prepared != prepared) {
          return std::array<std::string, 2>{};
        }
        return std::array<std::string, 2>{
            ComputeArcImportTreeFingerprint(before),
            ComputeArcImportTreeFingerprint(current)};
      },
      profile_->GetPath(), context->prepared, context->previous_tree,
      context->start_tree);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(task),
      base::BindOnce(&ArcImportService::OnManualRecoveryFingerprints,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnManualRecoveryFingerprints(
    std::unique_ptr<ManualRecoveryContext> context,
    std::array<std::string, 2> fingerprints) {
  tab_tree::TabTreeSnapshot live;
  if (!IsArcImportTreeFingerprint(fingerprints[0]) ||
      !IsArcImportTreeFingerprint(fingerprints[1]) ||
      fingerprints[0] != context->prepared.previous_tree_sha256 ||
      (fingerprints[1] != context->prepared.expected_tree_sha256 &&
       fingerprints[1] != context->prepared.previous_tree_sha256) ||
      HasAffectedLiveTabs(context->prepared, context->removed_workspaces) ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != context->start_tree || !profile_) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  auto* service = SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!service) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  // Current Session is rebuilt from all native windows, not an old last-session
  // file. Recovery never closes a tab or rolls native state back independently.
  service->ResetFlushAndReadCurrentSessionForVerification(
      base::BindOnce(&ArcImportService::OnManualRecoveryNativeReadback,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnManualRecoveryNativeReadback(
    std::unique_ptr<ManualRecoveryContext> context,
    std::vector<std::unique_ptr<sessions::SessionWindow>> windows,
    SessionID /*active_window_id*/,
    bool read_error) {
  const std::set<std::string> affected(context->prepared.affected_ids.begin(),
                                       context->prepared.affected_ids.end());
  bool native_absent = !read_error;
  for (const auto& window : windows) {
    if (!window) {
      native_absent = false;
      break;
    }
    for (const auto& tab : window->tabs) {
      if (!tab) {
        native_absent = false;
        break;
      }
      const auto found =
          tab->extra_data.find(session::kTabSessionMetadataExtraDataKey);
      if (found == tab->extra_data.end()) {
        continue;
      }
      session::TabSessionMetadata metadata;
      if (session::DecodeTabSessionMetadata(found->second, &metadata) !=
              session::SessionMetadataDecodeResult::kSuccess ||
          std::ranges::contains(context->removed_workspaces,
                                metadata.workspace_id) ||
          (metadata.tree_node_id &&
           affected.contains(metadata.tree_node_id->AsLowercaseString()))) {
        native_absent = false;
        break;
      }
    }
  }
  tab_tree::TabTreeSnapshot live;
  if (!native_absent ||
      HasAffectedLiveTabs(context->prepared, context->removed_workspaces) ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != context->start_tree ||
      (live != context->previous_tree &&
       session_bridge_->ApplySyncedTabTreeSnapshot(context->previous_tree) !=
           tab_tree::TabTreeStore::Result::kOk)) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  session_bridge_->FlushPersistenceForBackup(
      base::BindOnce(&ArcImportService::OnManualRecoveryFlushed,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnManualRecoveryFlushed(
    std::unique_ptr<ManualRecoveryContext> context,
    bool success) {
  tab_tree::TabTreeSnapshot live;
  if (!success ||
      HasAffectedLiveTabs(context->prepared, context->removed_workspaces) ||
      !session_bridge_->ExportTabTreeSnapshot(&live) ||
      live != context->previous_tree) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  if (!profile_) {
    FinishManualRecoveryRejected(std::move(context->callback));
    return;
  }
  auto task = base::BindOnce(
      [](base::FilePath path, ArcImportPreparedState expected) {
        const auto current = ReadArcImportJournal(path);
        return current.status == ArcImportStatus::kOk && current.prepared &&
               *current.prepared == expected &&
               RestoreArcImportJournalAfterRollback(
                   path, expected.previous_committed);
      },
      profile_->GetPath(), context->prepared);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(task),
      base::BindOnce(&ArcImportService::OnManualRecoveryJournalRestored,
                     weak_factory_.GetWeakPtr(), std::move(context->callback)));
}

void ArcImportService::OnManualRecoveryJournalRestored(
    ArcImportPreviewCallback callback,
    bool success) {
  operation_in_progress_ = false;
  // Recovery does not launch a new discovery/import. The user may inspect the
  // restored tree before explicitly requesting a fresh preview.
  std::move(callback).Run({.status = success
                                         ? ArcImportStatus::kOk
                                         : ArcImportStatus::kRecoveryRequired});
}

void ArcImportService::FinishManualRecoveryRejected(
    ArcImportPreviewCallback callback) {
  operation_in_progress_ = false;
  std::move(callback).Run({.status = ArcImportStatus::kRecoveryRequired});
}

}  // namespace ahoi::importer::arc
