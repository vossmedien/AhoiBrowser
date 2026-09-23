// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ahoi/browser/session/portable_workspace_bundle.h"
#include "ahoi/browser/session/portable_workspace_structure.h"
#include "ahoi/browser/session/session_bridge.h"
#include "ahoi/browser/session/session_bridge_factory.h"
#include "ahoi/browser/session/workspace_structure_state.h"
#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
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
      !args[3].is_bool() || portable_export_dialog_ ||
      portable_export_writing_) {
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
      portable_export_json_.empty() || portable_export_dialog_ ||
      portable_export_writing_) {
    ResolveJavascriptCallback(args.front().Clone(),
                              base::Value(std::move(result)));
    return;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  portable_export_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(contents));
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions.resize(1);
  types.extensions[0].push_back(FILE_PATH_LITERAL("json"));
  const base::FilePath suggested =
      base::GetHomeDir()
          .AppendASCII("Downloads")
          .Append(FILE_PATH_LITERAL("AhoiBrowser-Workspaces.ahoi.json"));
  portable_export_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(), suggested,
      &types, 0, FILE_PATH_LITERAL("json"), contents->GetTopLevelNativeWindow(),
      nullptr);
  result.Set("status", "dialog");
  ResolveJavascriptCallback(args.front().Clone(),
                            base::Value(std::move(result)));
}

void AhoiSettingsHandler::FileSelected(const ui::SelectedFileInfo& file,
                                       int /*index*/) {
  portable_export_dialog_ = nullptr;
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
  portable_export_dialog_ = nullptr;
  if (IsJavascriptAllowed() && IsAuthorizedSettingsPage()) {
    base::DictValue result;
    result.Set("status", "cancelled");
    FireWebUIListener("ahoi-portable-export-result",
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
