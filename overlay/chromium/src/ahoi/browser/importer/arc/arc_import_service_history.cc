// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include "ahoi/browser/importer/arc/arc_history_import_runner.h"
#include "ahoi/browser/importer/arc/arc_import_service.h"
#include "ahoi/browser/importer/arc/arc_import_service_internal.h"
#include "base/functional/bind.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/common/pref_names.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"

namespace ahoi::importer::arc {

ArcHistoryImportRunner* ArcImportService::GetHistoryRunner() {
  if (!history_runner_ && profile_) {
    // Explicit, user-confirmed import into this regular profile.
    history::HistoryService* history_service =
        HistoryServiceFactory::GetForProfile(
            profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (history_service) {
      history_runner_ =
          std::make_unique<ArcHistoryImportRunner>(history_service);
    }
  }
  return history_runner_.get();
}

bool ArcImportService::CanImportHistory() const {
  return profile_ && profile_->GetPrefs() &&
         !profile_->GetPrefs()->GetBoolean(
             prefs::kSavingBrowserHistoryDisabled);
}

void ArcImportService::BeginHistoryRecovery(ArcImportPreviewCallback callback,
                                            ArcHistoryPreparedState prepared) {
  ArcHistoryImportRunner* runner = GetHistoryRunner();
  if (!runner || !profile_) {
    OnHistoryRecovered(std::move(callback),
                       ArcImportStatus::kTransactionFailed);
    return;
  }
  runner->Recover(
      profile_->GetPath(), std::move(prepared),
      base::BindOnce(&ArcImportService::OnHistoryRecovered,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ArcImportService::OnHistoryRecovered(ArcImportPreviewCallback callback,
                                          ArcImportStatus status) {
  operation_in_progress_ = false;
  if (status != ArcImportStatus::kOk) {
    // The prepared history marker stays and the next discovery retries.
    // kRecoveryRequired is reserved for the sidebar's manual recovery flow.
    std::move(callback).Run(
        {.status = status == ArcImportStatus::kRecoveryRequired
                       ? ArcImportStatus::kTransactionFailed
                       : status});
    return;
  }
  DiscoverAndPreview(ArcImportPlanOptions(), std::move(callback));
}

void ArcImportService::StartHistoryWithoutSidebarChange(
    ArcImportCommitCallback callback,
    ArcImportCommitResult result,
    bool import_sidebar,
    ArcSource selected_source,
    std::string snapshot_token) {
  auto context = std::make_unique<CommitContext>();
  context->callback = std::move(callback);
  context->result = std::move(result);
  context->selected_source = std::move(selected_source);
  context->snapshot_hash = std::move(snapshot_token);
  context->import_sidebar = import_sidebar;
  context->import_history = true;
  FinishCommit(std::move(context));
}

void ArcImportService::FinishCommit(std::unique_ptr<CommitContext> context) {
  if (!context->import_history) {
    operation_in_progress_ = false;
    std::move(context->callback).Run(std::move(context->result));
    return;
  }
  ArcHistoryImportRunner* runner =
      CanImportHistory() ? GetHistoryRunner() : nullptr;
  if (!runner || !profile_) {
    OnHistoryImported(
        std::move(context),
        {.selected = true, .status = ArcImportStatus::kTransactionFailed});
    return;
  }
  ArcHistoryImportRequest request{
      .profile_path = profile_->GetPath(),
      .selected_source = context->selected_source,
      .snapshot_token = context->snapshot_hash,
      .backup_identifier = context->prepared.backup_identifier,
      .manifest_sha256 = context->prepared.manifest_sha256};
  runner->Import(
      std::move(request),
      base::BindOnce(&ArcImportService::OnHistoryImported,
                     weak_factory_.GetWeakPtr(), std::move(context)));
}

void ArcImportService::OnHistoryImported(std::unique_ptr<CommitContext> context,
                                         ArcHistoryImportResult history) {
  history.selected = true;
  ArcImportCommitResult& result = context->result;
  if (!context->import_sidebar) {
    // kRecoveryRequired names the sidebar's manual recovery in the UI; an
    // unfinished history import is completed by the next discovery instead.
    result.status = history.status == ArcImportStatus::kRecoveryRequired
                        ? ArcImportStatus::kTransactionFailed
                        : history.status;
  } else if (history.status == ArcImportStatus::kOk) {
    // The committed sidebar result stays authoritative; a history failure is
    // reported in its own field and never rolls the sidebar back.
    result.status = ArcImportStatus::kOk;
  }
  result.history = history;
  operation_in_progress_ = false;
  std::move(context->callback).Run(std::move(context->result));
}

}  // namespace ahoi::importer::arc
