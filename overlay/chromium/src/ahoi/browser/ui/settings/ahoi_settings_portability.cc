// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/portable_workspace_bundle.h"
#include "ahoi/browser/session/portable_workspace_import_destination.h"
#include "ahoi/browser/session/portable_workspace_import_plan.h"
#include "ahoi/browser/session/portable_workspace_structure.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace ahoi::settings {
namespace {

bool HasCallbackId(const base::ListValue& args) {
  return !args.empty() && args.front().is_string() &&
         !args.front().GetString().empty();
}

base::DictValue ExportLabels() {
  const bool german = base::i18n::GetConfiguredLocale().starts_with("de");
  const auto label = [german](const char* de, const char* en) {
    return std::string(german ? de : en);
  };
  base::DictValue labels;
  labels.Set("title",
             label("Workspace-Daten exportieren", "Export workspace data"));
  labels.Set("description",
             label("Wähle Workspaces und optionale Kategorien. Die Datei "
                   "enthält nur Browserstruktur und wird nicht hochgeladen.",
                   "Choose workspaces and optional categories. The file "
                   "contains browser structure only and is not uploaded."));
  labels.Set("temporary",
             label("Temporäre Tabs einschließen", "Include temporary tabs"));
  labels.Set("archives", label("Archive einschließen", "Include archives"));
  labels.Set("prepare", label("Vorschau erstellen", "Preview export"));
  labels.Set("save", label("Datei sichern…", "Save file…"));
  labels.Set("unencrypted",
             label("Diese Datei ist nicht verschlüsselt. Prüfe Ziel und "
                   "Auslassungen vor dem Sichern.",
                   "This file is not encrypted. Review the destination and "
                   "omissions before saving."));
  labels.Set("workspaces", label("Workspaces", "Workspaces"));
  labels.Set("pages", label("Seiten", "Pages"));
  labels.Set("splits", label("Splits", "Splits"));
  labels.Set("archivesCount", label("Archive", "Archives"));
  labels.Set("excluded", label("Ausgelassen", "Omitted"));
  labels.Set("saved", label("Datei gesichert", "File saved"));
  labels.Set("cancelled", label("Sichern abgebrochen", "Save cancelled"));
  labels.Set("failed", label("Export nicht möglich", "Export unavailable"));
  labels.Set("importFile",
             label("Workspace-Datei prüfen…", "Inspect workspace file…"));
  labels.Set("importReady", label("Datei geprüft – noch nichts importiert",
                                  "File inspected – nothing imported yet"));
  labels.Set("importFailed", label("Datei nicht lesbar oder nicht unterstützt",
                                   "File unreadable or unsupported"));
  labels.Set("importNew", label("Neu", "New"));
  labels.Set("importIdentical",
             label("Bereits identisch", "Already identical"));
  labels.Set("importConflict", label("Konflikt – Entscheidung nötig",
                                     "Conflict – decision required"));
  labels.Set("importDestination", label("Zielvorschau – noch kein Import",
                                        "Destination preview – not imported"));
  labels.Set("importTargetUnavailable",
             label("Zielprofil gerade nicht verfügbar",
                   "Destination profile currently unavailable"));
  labels.Set("importCommit", label("Ausgewählte Workspaces importieren",
                                   "Import selected workspaces"));
  labels.Set("importSelectionHint",
             label("Wähle die Ziele. Workspaces mit Konflikten bleiben "
                   "abgewählt; vorhandene Daten werden nicht ersetzt.",
                   "Choose destinations. Conflicting workspaces stay "
                   "unselected; existing data is not replaced."));
  labels.Set("imported", label("Import abgeschlossen", "Import complete"));
  labels.Set("noChanges", label("Bereits vorhanden – keine Änderungen",
                                "Already present – no changes"));
  labels.Set("importChanged",
             label("Ziel geändert – Datei erneut prüfen",
                   "Destination changed – inspect file again"));
  labels.Set("importCommitFailed",
             label("Import nicht abgeschlossen – Ergebnis prüfen",
                   "Import not completed – review result"));
  return labels;
}

bool WritePortableFile(base::FilePath path, std::string bytes) {
  return base::ImportantFileWriter::WriteFileAtomically(path, bytes);
}

}  // namespace

void AhoiSettingsHandler::HandleGetPortableExportOptions(
    const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args) ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  base::DictValue result;
  result.Set("labels", ExportLabels());
  result.Set("available", false);
  base::ListValue workspaces;
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile_);
  tab_tree::TabTreeSnapshot snapshot;
  if (bridge && bridge->ExportTabTreeSnapshot(&snapshot)) {
    for (const auto& workspace : snapshot.workspaces) {
      if (workspace.tombstone) {
        continue;
      }
      base::DictValue value;
      value.Set("id", workspace.id.AsLowercaseString());
      value.Set("name", base::UTF16ToUTF8(workspace.name));
      workspaces.Append(std::move(value));
    }
    result.Set("available", !workspaces.empty());
  }
  result.Set("workspaces", std::move(workspaces));
  ResolveJavascriptCallback(args.front().Clone(),
                            base::Value(std::move(result)));
}

void AhoiSettingsHandler::HandlePreparePortableExport(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  base::DictValue result;
  result.Set("status", "blocked");
  if (args.size() != 4u || !args[1].is_list() ||
      args[1].GetList().size() > 128u || !args[2].is_bool() ||
      !args[3].is_bool() || portable_file_dialog_ || portable_export_writing_ ||
      portable_import_reading_ || portable_import_committing_) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  portable_export_token_.clear();
  portable_export_json_.clear();
  std::vector<base::Uuid> selected;
  for (const base::Value& raw : args[1].GetList()) {
    if (!raw.is_string()) {
      ResolveJavascriptCallback(args.front().Clone(),
                                base::Value(std::move(result)));
      return;
    }
    base::Uuid id = base::Uuid::ParseLowercase(raw.GetString());
    if (!id.is_valid()) {
      ResolveJavascriptCallback(args.front().Clone(),
                                base::Value(std::move(result)));
      return;
    }
    selected.push_back(std::move(id));
  }
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile_);
  tab_tree::TabTreeSnapshot snapshot;
  if (!bridge || !bridge->ExportTabTreeSnapshot(&snapshot) ||
      !bridge->tab_tree_store()) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  const auto encoded_state =
      bridge->tab_tree_store()->ReadWorkspaceStructureState();
  if (!encoded_state) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  session::WorkspaceStructureState structure;
  if (!encoded_state->empty()) {
    auto decoded = session::DecodeWorkspaceStructureState(*encoded_state);
    if (!decoded) {
      ResolveJavascriptCallback(args.front().Clone(),
                                base::Value(std::move(result)));
      return;
    }
    structure = std::move(*decoded);
  }
  auto chosen = session::SelectPortableWorkspaceStructure(
      snapshot, structure, selected, args[2].GetBool(), args[3].GetBool());
  auto bytes =
      chosen ? session::EncodePortableWorkspaceBundle(*chosen) : std::nullopt;
  if (!bytes) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  portable_export_json_ = std::move(*bytes);
  portable_export_token_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  result.Set("status", "ready");
  result.Set("token", portable_export_token_);
  result.Set("bytes", static_cast<int>(portable_export_json_.size()));
  result.Set("workspaces", static_cast<int>(chosen->tree.workspaces.size()));
  result.Set("pages", static_cast<int>(std::ranges::count_if(
                          chosen->tree.nodes, [](const auto& node) {
                            return node.type ==
                                   tab_tree::TreeNodeType::kSavedPage;
                          })));
  result.Set("splits", static_cast<int>(chosen->splits.size()));
  result.Set("archives", static_cast<int>(chosen->archives.size()));
  result.Set("excluded",
             static_cast<int>(chosen->tree.excluded_temporary_pages +
                              chosen->tree.excluded_nonportable_pages +
                              chosen->tree.excluded_nonportable_home_targets +
                              chosen->excluded_incomplete_splits +
                              chosen->excluded_nonportable_archives));
  ResolveJavascriptCallback(args.front().Clone(),
                            base::Value(std::move(result)));
}

void AhoiSettingsHandler::HandleSavePortableExport(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  base::DictValue result;
  result.Set("status", "blocked");
  if (args.size() != 2u || !args[1].is_string() ||
      args[1].GetString() != portable_export_token_ ||
      portable_export_json_.empty() || portable_file_dialog_ ||
      portable_export_writing_ || portable_import_committing_) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  portable_dialog_purpose_ = PortableDialogPurpose::kExportSave;
  portable_file_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(contents));
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions.resize(1);
  types.extensions[0].push_back(FILE_PATH_LITERAL("json"));
  const base::FilePath suggested =
      base::GetHomeDir()
          .AppendASCII("Downloads")
          .Append(FILE_PATH_LITERAL("AhoiBrowser-Workspaces.ahoi.json"));
  portable_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(), suggested,
      &types, 0, FILE_PATH_LITERAL("json"), contents->GetTopLevelNativeWindow(),
      nullptr);
  result.Set("status", "dialog");
  ResolveJavascriptCallback(args.front().Clone(),
                            base::Value(std::move(result)));
}

void AhoiSettingsHandler::HandleOpenPortableImport(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  base::DictValue result;
  result.Set("status", "blocked");
  if (args.size() != 1u || portable_file_dialog_ || portable_export_writing_ ||
      portable_import_reading_ || portable_import_committing_) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  portable_import_token_.clear();
  portable_import_structure_.reset();
  content::WebContents* contents = web_ui()->GetWebContents();
  portable_dialog_purpose_ = PortableDialogPurpose::kImportOpen;
  portable_file_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(contents));
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions.resize(1);
  types.extensions[0].push_back(FILE_PATH_LITERAL("json"));
  portable_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
      base::GetHomeDir().AppendASCII("Downloads"), &types, 0,
      FILE_PATH_LITERAL("json"), contents->GetTopLevelNativeWindow(), nullptr);
  result.Set("status", "dialog");
  ResolveJavascriptCallback(args.front().Clone(),
                            base::Value(std::move(result)));
}

AhoiSettingsHandler::PortableImportReadback
AhoiSettingsHandler::ReadPortableImportFile(base::FilePath path) {
  PortableImportReadback readback;
  for (base::FilePath cursor = path; !cursor.empty();
       cursor = cursor.DirName()) {
    if (base::IsLink(cursor)) {
      return readback;
    }
    if (cursor.DirName() == cursor) {
      break;
    }
  }
  base::File::Info info;
  if (!base::GetFileInfo(path, &info) || info.is_directory || info.size <= 0 ||
      info.size >
          static_cast<int64_t>(session::kMaximumPortableWorkspaceBundleBytes)) {
    return readback;
  }
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(
          path, &bytes, session::kMaximumPortableWorkspaceBundleBytes)) {
    return readback;
  }
  const auto decoded = session::DecodePortableWorkspaceBundle(bytes);
  if (!decoded) {
    return readback;
  }
  readback.structure = std::move(*decoded);
  return readback;
}

void AhoiSettingsHandler::OnPortableImportRead(
    PortableImportReadback readback) {
  portable_import_reading_ = false;
  portable_import_token_.clear();
  portable_import_structure_.reset();
  base::DictValue result;
  result.Set("status", readback.structure ? "targetUnavailable" : "failed");
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile_);
  tab_tree::TabTreeSnapshot current_tree;
  if (readback.structure && bridge && bridge->is_ready() &&
      bridge->ExportTabTreeSnapshot(&current_tree) &&
      bridge->tab_tree_store()) {
    const auto encoded_state =
        bridge->tab_tree_store()->ReadWorkspaceStructureState();
    const auto current_structure =
        encoded_state ? session::DecodeWorkspaceStructureState(*encoded_state)
                      : std::nullopt;
    if (current_structure) {
      const auto destination = session::AnalyzePortableWorkspaceDestination(
          *readback.structure, current_tree, *current_structure);
      const int new_workspaces = static_cast<int>(std::ranges::count_if(
          destination.workspaces, [](const auto& workspace) {
            return workspace.kind == session::PortableDestinationKind::kNew;
          }));
      const int identical_workspaces = static_cast<int>(std::ranges::count_if(
          destination.workspaces, [](const auto& workspace) {
            return workspace.kind ==
                   session::PortableDestinationKind::kIdentical;
          }));
      const int conflicting_workspaces =
          static_cast<int>(destination.workspaces.size()) - new_workspaces -
          identical_workspaces;
      result.Set("status", "preview");
      result.Set("pages",
                 static_cast<int>(std::ranges::count_if(
                     readback.structure->tree.nodes, [](const auto& node) {
                       return node.type == tab_tree::TreeNodeType::kSavedPage;
                     })));
      result.Set("splits", static_cast<int>(readback.structure->splits.size()));
      result.Set("archives",
                 static_cast<int>(readback.structure->archives.size()));
      result.Set("newItems",
                 new_workspaces + static_cast<int>(destination.new_nodes +
                                                   destination.new_splits +
                                                   destination.new_archives));
      result.Set("identicalItems",
                 identical_workspaces +
                     static_cast<int>(destination.identical_nodes +
                                      destination.identical_splits +
                                      destination.identical_archives));
      result.Set("conflictingItems",
                 conflicting_workspaces +
                     static_cast<int>(destination.conflicting_nodes +
                                      destination.conflicting_splits +
                                      destination.conflicting_archives));
      const bool has_safe_workspace = std::ranges::any_of(
          destination.workspaces, [](const auto& workspace) {
            return workspace.kind !=
                   session::PortableDestinationKind::kConflict;
          });
      result.Set("canImport", has_safe_workspace);
      if (has_safe_workspace) {
        portable_import_structure_ = std::move(readback.structure);
        portable_import_token_ =
            base::Uuid::GenerateRandomV4().AsLowercaseString();
        result.Set("token", portable_import_token_);
      }
      base::ListValue workspaces;
      for (const auto& workspace : destination.workspaces) {
        base::DictValue value;
        value.Set("id", workspace.id.AsLowercaseString());
        value.Set("name", base::TruncateUTF8ToByteSize(
                              base::UTF16ToUTF8(workspace.name), 256));
        value.Set(
            "destination",
            workspace.kind == session::PortableDestinationKind::kNew ? "new"
            : workspace.kind == session::PortableDestinationKind::kIdentical
                ? "identical"
                : "conflict");
        workspaces.Append(std::move(value));
      }
      result.Set("workspaces", std::move(workspaces));
    }
  }
  if (IsJavascriptAllowed() && IsAuthorizedSettingsPage()) {
    FireWebUIListener("ahoi-portable-import-result",
                      base::Value(std::move(result)));
  }
}

void AhoiSettingsHandler::HandleCommitPortableImport(
    const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  base::DictValue result;
  result.Set("status", "blocked");
  if (args.size() != 3u || !args[1].is_string() || !args[2].is_list() ||
      args[2].GetList().empty() || args[2].GetList().size() > 128u ||
      args[1].GetString() != portable_import_token_ ||
      portable_import_token_.empty() || !portable_import_structure_ ||
      portable_file_dialog_ || portable_import_reading_ ||
      portable_import_committing_ || portable_export_writing_) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  std::vector<base::Uuid> selected_workspace_ids;
  for (const auto& raw : args[2].GetList()) {
    if (!raw.is_string()) {
      ResolveJavascriptCallback(args.front().Clone(),
                                base::Value(std::move(result)));
      return;
    }
    const auto id = base::Uuid::ParseLowercase(raw.GetString());
    if (!id.is_valid()) {
      ResolveJavascriptCallback(args.front().Clone(),
                                base::Value(std::move(result)));
      return;
    }
    selected_workspace_ids.push_back(id);
  }
  auto selected = session::SelectPortableWorkspaceImport(
      *portable_import_structure_, selected_workspace_ids);
  if (!selected) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  SessionBridge* bridge = SessionBridgeFactory::GetForProfile(profile_);
  if (!bridge) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  portable_import_committing_ = true;
  portable_import_token_.clear();
  portable_import_lease_ = std::make_shared<std::atomic<bool>>(true);
  auto authorization = base::BindRepeating(
      [](std::shared_ptr<std::atomic<bool>> lease) {
        return lease->load(std::memory_order_acquire);
      },
      portable_import_lease_);
  bridge->CommitPortableWorkspaceImport(
      *selected, std::move(authorization),
      base::BindOnce(&AhoiSettingsHandler::OnPortableImportCommitted,
                     weak_factory_.GetWeakPtr(), args.front().Clone()));
}

void AhoiSettingsHandler::OnPortableImportCommitted(
    base::Value callback_id,
    SessionBridge::PortableImportResult outcome) {
  portable_import_committing_ = false;
  if (portable_import_lease_) {
    portable_import_lease_->store(false, std::memory_order_release);
  }
  portable_import_lease_.reset();
  portable_import_structure_.reset();
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  const char* status = "commitFailed";
  switch (outcome) {
    case SessionBridge::PortableImportResult::kImported:
      status = "imported";
      break;
    case SessionBridge::PortableImportResult::kNoChanges:
      status = "noChanges";
      break;
    case SessionBridge::PortableImportResult::kConflict:
      status = "conflict";
      break;
    case SessionBridge::PortableImportResult::kUnavailable:
      status = "targetUnavailable";
      break;
    case SessionBridge::PortableImportResult::kFailed:
      break;
  }
  base::DictValue result;
  result.Set("status", status);
  ResolveJavascriptCallback(callback_id, base::Value(std::move(result)));
}

void AhoiSettingsHandler::FileSelected(const ui::SelectedFileInfo& file,
                                       int /*index*/) {
  const PortableDialogPurpose purpose = portable_dialog_purpose_;
  portable_dialog_purpose_ = PortableDialogPurpose::kNone;
  portable_file_dialog_ = nullptr;
  if (purpose == PortableDialogPurpose::kImportOpen) {
    portable_import_reading_ = true;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&AhoiSettingsHandler::ReadPortableImportFile,
                       file.path()),
        base::BindOnce(&AhoiSettingsHandler::OnPortableImportRead,
                       weak_factory_.GetWeakPtr()));
    return;
  }
  if (purpose != PortableDialogPurpose::kExportSave) {
    return;
  }
  if (portable_export_json_.empty() || portable_export_writing_) {
    return;
  }
  portable_export_writing_ = true;
  portable_export_token_.clear();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WritePortableFile, file.path(),
                     std::move(portable_export_json_)),
      base::BindOnce(&AhoiSettingsHandler::OnPortableExportWritten,
                     weak_factory_.GetWeakPtr()));
}

void AhoiSettingsHandler::FileSelectionCanceled() {
  const PortableDialogPurpose purpose = portable_dialog_purpose_;
  portable_dialog_purpose_ = PortableDialogPurpose::kNone;
  portable_file_dialog_ = nullptr;
  if (IsJavascriptAllowed() && IsAuthorizedSettingsPage()) {
    base::DictValue result;
    result.Set("status", "cancelled");
    FireWebUIListener(purpose == PortableDialogPurpose::kImportOpen
                          ? "ahoi-portable-import-result"
                          : "ahoi-portable-export-result",
                      base::Value(std::move(result)));
  }
}

void AhoiSettingsHandler::OnPortableExportWritten(bool success) {
  portable_export_writing_ = false;
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  base::DictValue result;
  result.Set("status", success ? "saved" : "failed");
  FireWebUIListener("ahoi-portable-export-result",
                    base::Value(std::move(result)));
}

}  // namespace ahoi::settings
