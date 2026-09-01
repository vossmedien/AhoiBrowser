// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/arc/arc_import_handler.h"

#include <optional>
#include <string>
#include <utility>

#include "ahoi/browser/importer/arc/arc_import_service_factory.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

namespace ahoi::importer::arc {

namespace {

const char* StatusName(ArcImportStatus status) {
  switch (status) {
    case ArcImportStatus::kOk:
      return "ok";
    case ArcImportStatus::kNotFound:
      return "notFound";
    case ArcImportStatus::kInvalidPath:
      return "invalidPath";
    case ArcImportStatus::kUnsafeSymlink:
      return "unsafeSymlink";
    case ArcImportStatus::kNotRegularFile:
      return "notRegularFile";
    case ArcImportStatus::kIoError:
      return "ioError";
    case ArcImportStatus::kSourceChanged:
      return "sourceChanged";
    case ArcImportStatus::kLimitExceeded:
      return "limitExceeded";
    case ArcImportStatus::kInvalidJson:
      return "invalidJson";
    case ArcImportStatus::kUnsupportedSchema:
      return "unsupportedSchema";
    case ArcImportStatus::kMissingRequiredField:
      return "missingRequiredField";
    case ArcImportStatus::kMalformedSerializedMap:
      return "malformedSerializedMap";
    case ArcImportStatus::kDuplicateIdentifier:
      return "duplicateIdentifier";
    case ArcImportStatus::kGraphViolation:
      return "graphViolation";
    case ArcImportStatus::kInvalidText:
      return "invalidText";
    case ArcImportStatus::kNoImportableWorkspaces:
      return "noImportableWorkspaces";
    case ArcImportStatus::kStalePreview:
      return "stalePreview";
    case ArcImportStatus::kConflict:
      return "conflict";
    case ArcImportStatus::kNoChanges:
      return "noChanges";
    case ArcImportStatus::kTransactionFailed:
      return "transactionFailed";
    case ArcImportStatus::kRuntimeFailed:
      return "runtimeFailed";
    case ArcImportStatus::kJournalError:
      return "journalError";
    case ArcImportStatus::kSourceInUse:
      return "sourceInUse";
    case ArcImportStatus::kBackupError:
      return "backupError";
    case ArcImportStatus::kInsufficientDiskSpace:
      return "insufficientDiskSpace";
    case ArcImportStatus::kBackupQuotaExceeded:
      return "backupQuotaExceeded";
    case ArcImportStatus::kRecoveryRequired:
      return "recoveryRequired";
  }
  return "transactionFailed";
}

std::optional<ArcConflictResolution> ParseConflictResolution(
    const std::string& value) {
  if (value == "rename") {
    return ArcConflictResolution::kRename;
  }
  if (value == "skip") {
    return ArcConflictResolution::kSkip;
  }
  if (value == "merge") {
    return ArcConflictResolution::kMerge;
  }
  return std::nullopt;
}

base::DictValue StatsValue(const ArcImportStats& stats) {
  base::DictValue value;
  value.Set("sourceWorkspaces", static_cast<int>(stats.source_workspace_count));
  value.Set("sourceItems", static_cast<int>(stats.source_item_count));
  value.Set("workspaces", static_cast<int>(stats.imported_workspace_count));
  value.Set("folders", static_cast<int>(stats.imported_folder_count));
  value.Set("pages", static_cast<int>(stats.imported_page_count));
  value.Set("splits", static_cast<int>(stats.imported_split_count));
  value.Set("degradedSplits", static_cast<int>(stats.degraded_split_count));
  value.Set("topApps", static_cast<int>(stats.imported_global_top_app_count));
  value.Set("unsafeUrls", static_cast<int>(stats.skipped_unsafe_url_count));
  value.Set("unsupportedItems",
            static_cast<int>(stats.skipped_unsupported_item_count));
  value.Set("unreachableItems",
            static_cast<int>(stats.ignored_unreachable_item_count));
  value.Set("deduplicatedWorkspaces",
            static_cast<int>(stats.deduplicated_workspace_count));
  value.Set("deduplicatedItems",
            static_cast<int>(stats.deduplicated_item_count));
  value.Set("deduplicatedSplits",
            static_cast<int>(stats.deduplicated_split_count));
  return value;
}

}  // namespace

ArcImportHandler::ArcImportHandler(Profile* profile) : profile_(profile) {
  // Instantiate the profile-scoped authority when Settings creates its local
  // handler. Discovery remains fully user-triggered and mutation-free.
  ArcImportServiceFactory::GetForProfile(profile_);
}

ArcImportHandler::~ArcImportHandler() = default;

void ArcImportHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "ahoiArcDiscover", base::BindRepeating(&ArcImportHandler::HandleDiscover,
                                             base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "ahoiArcCommit", base::BindRepeating(&ArcImportHandler::HandleCommit,
                                           base::Unretained(this)));
}

void ArcImportHandler::HandleDiscover(const base::ListValue& args) {
  if (args.size() != 1u || !args.front().is_string()) {
    return;
  }
  AllowJavascript();
  base::Value callback_id = args.front().Clone();
  ArcImportService* service = ArcImportServiceFactory::GetForProfile(profile_);
  if (!service) {
    ResolvePreview(std::move(callback_id),
                   {.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  service->DiscoverAndPreview(base::BindOnce(&ArcImportHandler::ResolvePreview,
                                             weak_factory_.GetWeakPtr(),
                                             std::move(callback_id)));
}

void ArcImportHandler::HandleCommit(const base::ListValue& args) {
  if (args.size() != 8u || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string() || !args[3].is_list() || !args[4].is_bool() ||
      !args[5].is_bool() || !args[6].is_bool() || !args[7].is_bool()) {
    return;
  }
  AllowJavascript();
  base::Value callback_id = args[0].Clone();
  const std::optional<ArcConflictResolution> conflict_resolution =
      ParseConflictResolution(args[2].GetString());
  ArcImportSelection selection;
  selection.import_sidebar = args[4].GetBool();
  selection.reconstruct_splits = args[5].GetBool();
  selection.backup_confirmed = args[6].GetBool();
  selection.commit_confirmed = args[7].GetBool();
  for (const base::Value& profile : args[3].GetList()) {
    if (!profile.is_string()) {
      ResolveCommit(std::move(callback_id),
                    {.status = ArcImportStatus::kTransactionFailed});
      return;
    }
    selection.selected_browser_profiles.push_back(profile.GetString());
  }
  if (!conflict_resolution || !selection.backup_confirmed ||
      !selection.commit_confirmed) {
    ResolveCommit(std::move(callback_id),
                  {.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          web_ui()->GetWebContents());
  ArcImportService* service = ArcImportServiceFactory::GetForProfile(profile_);
  if (!browser || browser->GetProfile() != profile_ || !service) {
    ResolveCommit(std::move(callback_id),
                  {.status = ArcImportStatus::kTransactionFailed});
    return;
  }
  service->Commit(
      args[1].GetString(), *conflict_resolution, std::move(selection), browser,
      base::BindOnce(&ArcImportHandler::ResolveCommit,
                     weak_factory_.GetWeakPtr(), std::move(callback_id)));
}

void ArcImportHandler::ResolvePreview(base::Value callback_id,
                                      ArcImportPreview preview) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  base::DictValue value;
  value.Set("status", StatusName(preview.status));
  value.Set("snapshotToken", preview.snapshot_token);
  value.Set("stats", StatsValue(preview.stats));
  value.Set("conflictingWorkspaces",
            static_cast<int>(preview.conflicting_workspace_count));
  value.Set("alreadyImported", preview.already_imported);
  value.Set("sourceInUse", preview.arc_is_running);
  base::ListValue workspaces;
  for (const std::u16string& workspace : preview.target_workspace_names) {
    workspaces.Append(base::UTF16ToUTF8(workspace));
  }
  value.Set("targetWorkspaces", std::move(workspaces));
  base::ListValue profiles;
  for (const std::string& profile : preview.available_browser_profiles) {
    profiles.Append(profile);
  }
  value.Set("profiles", std::move(profiles));
  ResolveJavascriptCallback(callback_id, base::Value(std::move(value)));
}

void ArcImportHandler::ResolveCommit(base::Value callback_id,
                                     ArcImportCommitResult result) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  base::DictValue value;
  value.Set("status", StatusName(result.status));
  value.Set("stats", StatsValue(result.stats));
  value.Set("renamedWorkspaces",
            static_cast<int>(result.renamed_workspace_count));
  value.Set("skippedWorkspaces",
            static_cast<int>(result.skipped_workspace_count));
  value.Set("mergedWorkspaces",
            static_cast<int>(result.merged_workspace_count));
  value.Set("reconstructedSplits",
            static_cast<int>(result.reconstructed_split_count));
  value.Set("approximatedFourPaneRatios",
            static_cast<int>(result.approximated_four_pane_ratio_count));
  ResolveJavascriptCallback(callback_id, base::Value(std::move(value)));
}

}  // namespace ahoi::importer::arc
