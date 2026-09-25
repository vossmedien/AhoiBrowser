// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/shortcut_settings_model.h"

#include <optional>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/prefs/pref_service.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::settings {

namespace {

using shortcuts::ConflictKind;
using shortcuts::ShortcutCategory;
using shortcuts::ShortcutCommand;

bool German() {
  return base::i18n::GetConfiguredLocale().starts_with("de");
}

std::string Text(const char* de, const char* en) {
  return std::string(German() ? de : en);
}

std::string Title(const ShortcutCommand& command) {
  return base::UTF16ToUTF8(German() ? command.title_de : command.title_en);
}

std::string CategoryId(ShortcutCategory category) {
  switch (category) {
    case ShortcutCategory::kBrowser:
      return "browser";
    case ShortcutCategory::kTab:
      return "tab";
    case ShortcutCategory::kWorkspace:
      return "workspace";
    case ShortcutCategory::kSplit:
      return "split";
    case ShortcutCategory::kSidebar:
      return "sidebar";
  }
}

std::string CategoryLabel(ShortcutCategory category) {
  switch (category) {
    case ShortcutCategory::kBrowser:
      return Text("Browser", "Browser");
    case ShortcutCategory::kTab:
      return Text("Tabs", "Tabs");
    case ShortcutCategory::kWorkspace:
      return Text("Workspaces", "Workspaces");
    case ShortcutCategory::kSplit:
      return Text("Split", "Split");
    case ShortcutCategory::kSidebar:
      return Text("Seitenleiste", "Sidebar");
  }
}

std::string KeyName(ui::KeyboardCode key) {
  if ((key >= ui::VKEY_A && key <= ui::VKEY_Z) ||
      (key >= ui::VKEY_0 && key <= ui::VKEY_9)) {
    return std::string(1, static_cast<char>(key));
  }
  if (key >= ui::VKEY_F1 && key <= ui::VKEY_F24) {
    return "F" + base::NumberToString(key - ui::VKEY_F1 + 1);
  }
  switch (key) {
    case ui::VKEY_LEFT:
      return "←";
    case ui::VKEY_RIGHT:
      return "→";
    case ui::VKEY_UP:
      return "↑";
    case ui::VKEY_DOWN:
      return "↓";
    case ui::VKEY_SPACE:
      return Text("Leertaste", "Space");
    case ui::VKEY_TAB:
      return "⇥";
    case ui::VKEY_ESCAPE:
      return "⎋";
    case ui::VKEY_RETURN:
      return "↩";
    case ui::VKEY_BACK:
      return "⌫";
    case ui::VKEY_DELETE:
      return "⌦";
    case ui::VKEY_OEM_3:
      return "`";
    case ui::VKEY_OEM_COMMA:
      return ",";
    case ui::VKEY_OEM_PERIOD:
      return ".";
    case ui::VKEY_OEM_MINUS:
      return "-";
    case ui::VKEY_OEM_PLUS:
      return "=";
    case ui::VKEY_OEM_1:
      return ";";
    case ui::VKEY_OEM_2:
      return "/";
    case ui::VKEY_OEM_4:
      return "[";
    case ui::VKEY_OEM_5:
      return "\\";
    case ui::VKEY_OEM_6:
      return "]";
    case ui::VKEY_OEM_7:
      return "'";
    default:
      return "#" + base::NumberToString(static_cast<int>(key));
  }
}

base::ListValue KeyTexts(const std::vector<ui::Accelerator>& accelerators) {
  base::ListValue list;
  for (const ui::Accelerator& accelerator : accelerators) {
    list.Append(ShortcutKeyText(accelerator));
  }
  return list;
}

base::DictValue Labels() {
  base::DictValue labels;
  labels.Set("title", Text("Tastenkürzel", "Keyboard shortcuts"));
  labels.Set("description",
             Text("Menü, Command Bar und dieser Editor nutzen dieselbe "
                  "Liste. Eine belegte Taste wird nie still überschrieben.",
                  "Menus, the command bar and this editor share one list. A "
                  "key that is taken is never overwritten silently."));
  labels.Set("search", Text("Befehl suchen", "Search commands"));
  labels.Set("change", Text("Ändern", "Change"));
  labels.Set("recording",
             Text("Neue Tastenkombination drücken – Esc bricht ab",
                  "Press the new key combination – Esc cancels"));
  labels.Set("unbind", Text("Entfernen", "Remove"));
  labels.Set("reset", Text("Standard", "Default"));
  labels.Set("resetAll",
             Text("Alle auf Standard zurücksetzen", "Reset all to defaults"));
  labels.Set("none", Text("Keine Taste", "No key"));
  labels.Set("fixed",
             Text("Feste Taste", "Fixed key"));
  labels.Set("customized", Text("Geändert", "Changed"));
  labels.Set("defaultIs", Text("Standard:", "Default:"));
  labels.Set("noMatch", Text("Kein Befehl gefunden.", "No command found."));
  return labels;
}

std::optional<ui::Accelerator> AcceleratorFromPayload(
    const base::DictValue& payload) {
  const std::optional<int> key = payload.FindInt("keyCode");
  if (!key || *key <= 0 || *key > 0xFF) {
    return std::nullopt;
  }
  int modifiers = 0;
  if (payload.FindBool("cmd").value_or(false)) {
    modifiers |= ui::EF_COMMAND_DOWN;
  }
  if (payload.FindBool("ctrl").value_or(false)) {
    modifiers |= ui::EF_CONTROL_DOWN;
  }
  if (payload.FindBool("alt").value_or(false)) {
    modifiers |= ui::EF_ALT_DOWN;
  }
  if (payload.FindBool("shift").value_or(false)) {
    modifiers |= ui::EF_SHIFT_DOWN;
  }
  return ui::Accelerator(static_cast<ui::KeyboardCode>(*key), modifiers);
}

}  // namespace

std::string ShortcutKeyText(const ui::Accelerator& accelerator) {
  std::string text;
  if (accelerator.IsCtrlDown()) {
    text += "⌃";
  }
  if (accelerator.IsAltDown()) {
    text += "⌥";
  }
  if (accelerator.IsShiftDown()) {
    text += "⇧";
  }
  if (accelerator.IsCmdDown()) {
    text += "⌘";
  }
  return text + KeyName(accelerator.key_code());
}

base::DictValue BuildShortcutState(const shortcuts::Overrides& overrides,
                                   bool can_change) {
  base::ListValue commands;
  for (const ShortcutCommand& command : shortcuts::Catalog()) {
    base::DictValue item;
    item.Set("id", command.id);
    item.Set("category", CategoryId(command.category));
    item.Set("categoryLabel", CategoryLabel(command.category));
    item.Set("title", Title(command));
    item.Set("keys", KeyTexts(shortcuts::EffectiveAccelerators(overrides,
                                                               command.id)));
    item.Set("defaultKeys", KeyTexts(command.defaults));
    item.Set("customized", overrides.contains(command.id));
    item.Set("rebindable", command.rebindable);
    commands.Append(std::move(item));
  }
  base::DictValue state;
  state.Set("labels", Labels());
  state.Set("commands", std::move(commands));
  state.Set("canChange", can_change);
  return state;
}

ShortcutActionResult ApplyShortcutAction(
    PrefService* prefs,
    std::string_view action,
    const base::DictValue& payload,
    const shortcuts::ConflictSources& sources) {
  if (action == "resetAll") {
    shortcuts::ResetAll(prefs);
    return {};
  }
  const std::string* id = payload.FindString("id");
  if (!id) {
    return {.error = "invalidRequest"};
  }
  if (!shortcuts::FindCommand(*id)) {
    return {.error = "unknownCommand"};
  }
  if (action == "set") {
    const std::optional<ui::Accelerator> accelerator =
        AcceleratorFromPayload(payload);
    if (!accelerator) {
      return {.error = "invalidRequest"};
    }
    ShortcutActionResult result;
    if (!shortcuts::SetBinding(prefs, *id, *accelerator, sources,
                               &result.conflict)) {
      result.error = result.conflict.kind == ConflictKind::kNone
                         ? "writeFailed"
                         : "conflict";
    }
    return result;
  }
  if (action == "unbind") {
    return shortcuts::Unbind(prefs, *id) ? ShortcutActionResult()
                                         : ShortcutActionResult{
                                               .error = "writeFailed"};
  }
  if (action == "reset") {
    if (shortcuts::ResetToDefault(prefs, *id)) {
      return {};
    }
    // The default key now belongs to another command.
    return {.error = "conflict",
            .conflict = {.kind = ConflictKind::kOtherCommand}};
  }
  return {.error = "invalidRequest"};
}

std::string ShortcutErrorLabel(const ShortcutActionResult& result) {
  if (result.error.empty()) {
    return std::string();
  }
  if (result.error != "conflict") {
    return result.error == "unknownCommand"
               ? Text("Diesen Befehl gibt es nicht mehr.",
                      "This command no longer exists.")
               : Text("Die Änderung konnte nicht gespeichert werden.",
                      "The change could not be saved.");
  }
  switch (result.conflict.kind) {
    case ConflictKind::kOtherCommand: {
      const ShortcutCommand* other =
          shortcuts::FindCommand(result.conflict.other_command_id);
      if (!other) {
        return Text("Die Standardtaste gehört jetzt einem anderen Befehl. "
                    "Entferne sie dort zuerst.",
                    "The default key now belongs to another command. Remove "
                    "it there first.");
      }
      return base::StrCat(
          {Text("Diese Taste nutzt bereits „", "This key is already used by “"),
           Title(*other),
           Text("“. Entferne sie dort zuerst.", "”. Remove it there first.")});
    }
    case ConflictKind::kBrowserCommand:
      return Text("Diese Taste gehört einem Browserbefehl.",
                  "This key belongs to a browser command.");
    case ConflictKind::kExtension:
      return Text("Diese Taste nutzt eine Erweiterung. Ändere sie unter "
                  "chrome://extensions/shortcuts.",
                  "An extension uses this key. Change it at "
                  "chrome://extensions/shortcuts.");
    case ConflictKind::kReservedBySystem:
      return Text("macOS reserviert diese Taste.",
                  "macOS reserves this key.");
    case ConflictKind::kInvalid:
      return Text("Nutze ⌘, ⌃ oder eine Funktionstaste; ⌥ allein tippt "
                  "Sonderzeichen.",
                  "Use ⌘, ⌃ or a function key; ⌥ alone types characters.");
    case ConflictKind::kNotRebindable:
      return Text("Dieser Befehl hat eine feste Taste.",
                  "This command has a fixed key.");
    case ConflictKind::kNone:
      return std::string();
  }
}

}  // namespace ahoi::settings
