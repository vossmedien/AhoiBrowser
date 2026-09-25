// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/keyboard_shortcuts.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::shortcuts {

namespace {

constexpr int kModifierMask = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                              ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN;

ui::Accelerator Key(ui::KeyboardCode key, int modifiers) {
  return ui::Accelerator(key, modifiers);
}

ShortcutCommand Command(std::string id,
                        ShortcutCategory category,
                        std::u16string de,
                        std::u16string en,
                        std::vector<ui::Accelerator> defaults,
                        bool rebindable = true) {
  return ShortcutCommand{.id = std::move(id),
                         .category = category,
                         .title_de = std::move(de),
                         .title_en = std::move(en),
                         .defaults = std::move(defaults),
                         .rebindable = rebindable};
}

std::vector<ShortcutCommand> BuildCatalog() {
  constexpr int kCmd = ui::EF_COMMAND_DOWN;
  constexpr int kCtrl = ui::EF_CONTROL_DOWN;
  constexpr int kAlt = ui::EF_ALT_DOWN;
  constexpr int kShift = ui::EF_SHIFT_DOWN;
  std::vector<ShortcutCommand> catalog;
  catalog.push_back(Command(kCommandBar, ShortcutCategory::kBrowser,
                            u"Command Bar öffnen", u"Open the command bar",
                            {Key(ui::VKEY_L, kCmd), Key(ui::VKEY_T, kCmd)},
                            /*rebindable=*/false));
  catalog.push_back(Command(kQuickWindow, ShortcutCategory::kBrowser,
                            u"Quick Window öffnen", u"Open Quick Window",
                            {Key(ui::VKEY_SPACE, kAlt)},
                            /*rebindable=*/false));
  // New in the catalog. Option+Tab is layout independent (the key left of 1
  // is "^" on German keyboards), types no character, is free in Chromium and
  // macOS, and sits next to Control+Tab, which cycles tabs in order.
  catalog.push_back(Command(kSwitchToLastUsedTab, ShortcutCategory::kTab,
                            u"Zum zuletzt benutzten Tab",
                            u"Switch to the last used tab",
                            {Key(ui::VKEY_TAB, kAlt)}));
  catalog.push_back(Command(kSaveTab, ShortcutCategory::kTab,
                            u"Tab speichern", u"Save tab",
                            {Key(ui::VKEY_D, kCmd)}, /*rebindable=*/false));
  catalog.push_back(Command(kPreviousWorkspace, ShortcutCategory::kWorkspace,
                            u"Vorheriger Workspace", u"Previous Workspace",
                            {Key(ui::VKEY_LEFT, kCmd | kAlt)}));
  catalog.push_back(Command(kNextWorkspace, ShortcutCategory::kWorkspace,
                            u"Nächster Workspace", u"Next Workspace",
                            {Key(ui::VKEY_RIGHT, kCmd | kAlt)}));
  for (int i = 1; i <= 9; ++i) {
    const std::u16string number = base::NumberToString16(i);
    catalog.push_back(Command(
        kWorkspacePrefix + base::NumberToString(i),
        ShortcutCategory::kWorkspace, u"Workspace " + number,
        u"Workspace " + number,
        {Key(static_cast<ui::KeyboardCode>(ui::VKEY_0 + i), kCtrl)}));
  }
  catalog.push_back(Command(kToggleSidebarFloating, ShortcutCategory::kSidebar,
                            u"Seitenleiste schwebend", u"Float the sidebar",
                            {Key(ui::VKEY_S, kCmd | kShift)}));
  catalog.push_back(Command(kToggleSidebarVisibility,
                            ShortcutCategory::kSidebar,
                            u"Seitenleiste ein-/ausblenden",
                            u"Show or hide the sidebar",
                            {Key(ui::VKEY_H, kCmd | kShift)}));
  catalog.push_back(Command(kSidebarDiscovery, ShortcutCategory::kSidebar,
                            u"In der Seitenleiste suchen",
                            u"Search the sidebar",
                            {Key(ui::VKEY_F, kCmd | kShift)}));
  catalog.push_back(Command(kSidebarUndo, ShortcutCategory::kSidebar,
                            u"Seitenleiste: Rückgängig", u"Sidebar: Undo",
                            {Key(ui::VKEY_Z, kCmd)}, /*rebindable=*/false));
  for (int i = 1; i <= 4; ++i) {
    const std::u16string number = base::NumberToString16(i);
    catalog.push_back(Command(
        kSplitPanePrefix + base::NumberToString(i), ShortcutCategory::kSplit,
        u"Split: Bereich " + number, u"Split: pane " + number,
        {Key(static_cast<ui::KeyboardCode>(ui::VKEY_0 + i), kCmd | kCtrl)}));
  }
  catalog.push_back(Command(kSplitMovePaneLeft, ShortcutCategory::kSplit,
                            u"Split: Bereich nach links",
                            u"Split: move pane left",
                            {Key(ui::VKEY_LEFT, kCmd | kCtrl | kShift)}));
  catalog.push_back(Command(kSplitMovePaneRight, ShortcutCategory::kSplit,
                            u"Split: Bereich nach rechts",
                            u"Split: move pane right",
                            {Key(ui::VKEY_RIGHT, kCmd | kCtrl | kShift)}));
  catalog.push_back(Command(kSplitShrink, ShortcutCategory::kSplit,
                            u"Split: Teilung nach links",
                            u"Split: move divider left",
                            {Key(ui::VKEY_LEFT, kCmd | kCtrl)}));
  catalog.push_back(Command(kSplitGrow, ShortcutCategory::kSplit,
                            u"Split: Teilung nach rechts",
                            u"Split: move divider right",
                            {Key(ui::VKEY_RIGHT, kCmd | kCtrl)}));
  catalog.push_back(Command(kSplitCycleLayout, ShortcutCategory::kSplit,
                            u"Split: Anordnung wechseln",
                            u"Split: change layout",
                            {Key(ui::VKEY_L, kCmd | kCtrl)}));
  catalog.push_back(Command(kSplitRemove, ShortcutCategory::kSplit,
                            u"Split aufheben", u"Unsplit",
                            {Key(ui::VKEY_0, kCmd | kCtrl)}));
  catalog.push_back(Command(kSplitClosePane, ShortcutCategory::kSplit,
                            u"Split: Bereich schließen",
                            u"Split: close pane",
                            {Key(ui::VKEY_W, kCmd | kCtrl)}));
  return catalog;
}

bool SameKey(const ui::Accelerator& a, const ui::Accelerator& b) {
  return a.key_code() == b.key_code() &&
         (a.modifiers() & kModifierMask) == (b.modifiers() & kModifierMask);
}

bool IsModifierKey(ui::KeyboardCode key) {
  switch (key) {
    case ui::VKEY_SHIFT:
    case ui::VKEY_CONTROL:
    case ui::VKEY_MENU:
    case ui::VKEY_LWIN:
    case ui::VKEY_RWIN:
    case ui::VKEY_CAPITAL:
    case ui::VKEY_UNKNOWN:
      return true;
    default:
      return false;
  }
}

bool IsFunctionKey(ui::KeyboardCode key) {
  return key >= ui::VKEY_F1 && key <= ui::VKEY_F24;
}

bool IsValid(const ui::Accelerator& accelerator) {
  const ui::KeyboardCode key = accelerator.key_code();
  if (IsModifierKey(key)) {
    return false;
  }
  const int modifiers = accelerator.modifiers() & kModifierMask;
  if (modifiers & (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN)) {
    return true;
  }
  // Option alone (with or without Shift) types characters on macOS with
  // printable keys; function, editing and navigation keys stay free of that.
  if (modifiers & ui::EF_ALT_DOWN) {
    switch (key) {
      case ui::VKEY_SPACE:
      case ui::VKEY_TAB:
      case ui::VKEY_ESCAPE:
      case ui::VKEY_RETURN:
      case ui::VKEY_BACK:
      case ui::VKEY_DELETE:
      case ui::VKEY_LEFT:
      case ui::VKEY_RIGHT:
      case ui::VKEY_UP:
      case ui::VKEY_DOWN:
      case ui::VKEY_HOME:
      case ui::VKEY_END:
      case ui::VKEY_PRIOR:
      case ui::VKEY_NEXT:
        return true;
      default:
        return IsFunctionKey(key);
    }
  }
  return IsFunctionKey(key);
}

std::optional<std::vector<ui::Accelerator>> DecodeList(
    const base::Value& value) {
  const base::ListValue* list = value.GetIfList();
  if (!list) {
    return std::nullopt;
  }
  std::vector<ui::Accelerator> accelerators;
  for (const base::Value& item : *list) {
    const std::string* text = item.GetIfString();
    std::optional<ui::Accelerator> accelerator =
        text ? Parse(*text) : std::nullopt;
    if (!accelerator) {
      return std::nullopt;
    }
    accelerators.push_back(*accelerator);
  }
  return accelerators;
}

void WriteOverride(PrefService* prefs,
                   std::string_view id,
                   const std::vector<ui::Accelerator>& accelerators) {
  base::ListValue list;
  for (const ui::Accelerator& accelerator : accelerators) {
    list.Append(Serialize(accelerator));
  }
  ScopedDictPrefUpdate(prefs, kShortcutBindingsPref)->Set(id, std::move(list));
}

bool German() {
  return base::i18n::GetConfiguredLocale().starts_with("de");
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
      return German() ? "Leertaste" : "Space";
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

}  // namespace

std::u16string CommandTitle(const ShortcutCommand& command) {
  return German() ? command.title_de : command.title_en;
}

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

const std::vector<ShortcutCommand>& Catalog() {
  static const base::NoDestructor<std::vector<ShortcutCommand>> catalog(
      BuildCatalog());
  return *catalog;
}

const ShortcutCommand* FindCommand(std::string_view id) {
  for (const ShortcutCommand& command : Catalog()) {
    if (command.id == id) {
      return &command;
    }
  }
  return nullptr;
}

std::optional<size_t> IndexedCommandIndex(std::string_view id,
                                          std::string_view prefix) {
  if (!base::StartsWith(id, prefix)) {
    return std::nullopt;
  }
  unsigned number = 0;
  if (!base::StringToUint(id.substr(prefix.size()), &number) || number == 0) {
    return std::nullopt;
  }
  return static_cast<size_t>(number - 1);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kShortcutBindingsPref);
}

Overrides ReadOverrides(const PrefService& prefs) {
  Overrides overrides;
  if (!prefs.FindPreference(kShortcutBindingsPref)) {
    return overrides;
  }
  for (const auto [id, value] : prefs.GetDict(kShortcutBindingsPref)) {
    const ShortcutCommand* command = FindCommand(id);
    if (!command || !command->rebindable) {
      continue;  // Unknown or fixed commands keep their defaults.
    }
    if (std::optional<std::vector<ui::Accelerator>> list = DecodeList(value)) {
      overrides.emplace(id, std::move(*list));
    }
  }
  return overrides;
}

std::vector<ui::Accelerator> EffectiveAccelerators(const Overrides& overrides,
                                                   std::string_view id) {
  if (auto it = overrides.find(id); it != overrides.end()) {
    return it->second;
  }
  const ShortcutCommand* command = FindCommand(id);
  return command ? command->defaults : std::vector<ui::Accelerator>();
}

std::optional<std::string> CommandForAccelerator(
    const Overrides& overrides,
    const ui::Accelerator& accelerator) {
  for (const ShortcutCommand& command : Catalog()) {
    for (const ui::Accelerator& bound :
         EffectiveAccelerators(overrides, command.id)) {
      if (SameKey(bound, accelerator)) {
        return command.id;
      }
    }
  }
  return std::nullopt;
}

bool IsReservedBySystem(const ui::Accelerator& accelerator) {
  constexpr int kCmd = ui::EF_COMMAND_DOWN;
  constexpr int kCtrl = ui::EF_CONTROL_DOWN;
  constexpr int kAlt = ui::EF_ALT_DOWN;
  constexpr int kShift = ui::EF_SHIFT_DOWN;
  // macOS standard shortcuts an app must not take over: app switching,
  // hiding, minimizing, quitting, Spotlight, input sources, screenshots,
  // Force Quit, screen lock, Mission Control, Spaces and the emoji picker.
  static constexpr std::array<std::pair<ui::KeyboardCode, int>, 22> kReserved{{
      {ui::VKEY_TAB, kCmd},
      {ui::VKEY_TAB, kCmd | kShift},
      {ui::VKEY_OEM_3, kCmd},
      {ui::VKEY_OEM_3, kCmd | kShift},
      {ui::VKEY_Q, kCmd},
      {ui::VKEY_H, kCmd},
      {ui::VKEY_H, kCmd | kAlt},
      {ui::VKEY_M, kCmd},
      {ui::VKEY_M, kCmd | kAlt},
      {ui::VKEY_SPACE, kCmd},
      {ui::VKEY_SPACE, kCmd | kAlt},
      {ui::VKEY_SPACE, kCtrl},
      {ui::VKEY_SPACE, kCtrl | kAlt},
      {ui::VKEY_SPACE, kCmd | kCtrl},
      {ui::VKEY_3, kCmd | kShift},
      {ui::VKEY_4, kCmd | kShift},
      {ui::VKEY_5, kCmd | kShift},
      {ui::VKEY_ESCAPE, kCmd | kAlt},
      {ui::VKEY_Q, kCmd | kCtrl},
      {ui::VKEY_UP, kCtrl},
      {ui::VKEY_DOWN, kCtrl},
      {ui::VKEY_LEFT, kCtrl},
  }};
  const int modifiers = accelerator.modifiers() & kModifierMask;
  if (accelerator.key_code() == ui::VKEY_RIGHT && modifiers == kCtrl) {
    return true;
  }
  return std::ranges::any_of(kReserved, [&](const auto& reserved) {
    return reserved.first == accelerator.key_code() &&
           reserved.second == modifiers;
  });
}

Conflict CheckBinding(const Overrides& overrides,
                      std::string_view id,
                      const ui::Accelerator& accelerator,
                      const ConflictSources& sources) {
  const ShortcutCommand* command = FindCommand(id);
  if (!command || !command->rebindable) {
    return {.kind = ConflictKind::kNotRebindable};
  }
  if (!IsValid(accelerator)) {
    return {.kind = ConflictKind::kInvalid};
  }
  if (IsReservedBySystem(accelerator)) {
    return {.kind = ConflictKind::kReservedBySystem};
  }
  if (std::optional<std::string> owner =
          CommandForAccelerator(overrides, accelerator)) {
    if (*owner == id) {
      return {};
    }
    return {.kind = ConflictKind::kOtherCommand,
            .other_command_id = std::move(*owner)};
  }
  if (sources.is_extension_accelerator &&
      sources.is_extension_accelerator.Run(accelerator)) {
    return {.kind = ConflictKind::kExtension};
  }
  if (sources.is_browser_accelerator &&
      sources.is_browser_accelerator.Run(accelerator)) {
    return {.kind = ConflictKind::kBrowserCommand};
  }
  return {};
}

bool SetBinding(PrefService* prefs,
                std::string_view id,
                const ui::Accelerator& accelerator,
                const ConflictSources& sources,
                Conflict* conflict) {
  if (!prefs || !prefs->FindPreference(kShortcutBindingsPref) ||
      prefs->IsManagedPreference(kShortcutBindingsPref)) {
    return false;
  }
  const Overrides overrides = ReadOverrides(*prefs);
  const Conflict result = CheckBinding(overrides, id, accelerator, sources);
  if (conflict) {
    *conflict = result;
  }
  if (result.kind != ConflictKind::kNone) {
    return false;
  }
  const ui::Accelerator stored(accelerator.key_code(),
                               accelerator.modifiers() & kModifierMask);
  // A command holds one binding; the new key replaces its previous ones.
  WriteOverride(prefs, id, {stored});
  return true;
}

bool Unbind(PrefService* prefs, std::string_view id) {
  const ShortcutCommand* command = FindCommand(id);
  if (!prefs || !command || !command->rebindable ||
      !prefs->FindPreference(kShortcutBindingsPref) ||
      prefs->IsManagedPreference(kShortcutBindingsPref)) {
    return false;
  }
  WriteOverride(prefs, id, {});
  return true;
}

bool ResetToDefault(PrefService* prefs, std::string_view id) {
  const ShortcutCommand* command = FindCommand(id);
  if (!prefs || !command || !prefs->FindPreference(kShortcutBindingsPref) ||
      prefs->IsManagedPreference(kShortcutBindingsPref)) {
    return false;
  }
  // The default may meanwhile be bound to another command; that command
  // keeps it, and this one is not reset instead of taking it silently.
  Overrides others = ReadOverrides(*prefs);
  others.erase(std::string(id));
  for (const ui::Accelerator& accelerator : command->defaults) {
    const std::optional<std::string> owner =
        CommandForAccelerator(others, accelerator);
    if (owner && *owner != id) {
      return false;
    }
  }
  ScopedDictPrefUpdate(prefs, kShortcutBindingsPref)->Remove(id);
  return true;
}

void ResetAll(PrefService* prefs) {
  if (prefs && prefs->FindPreference(kShortcutBindingsPref) &&
      !prefs->IsManagedPreference(kShortcutBindingsPref)) {
    prefs->ClearPref(kShortcutBindingsPref);
  }
}

std::string Serialize(const ui::Accelerator& accelerator) {
  std::vector<std::string_view> parts;
  const int modifiers = accelerator.modifiers();
  if (modifiers & ui::EF_COMMAND_DOWN) {
    parts.push_back("cmd");
  }
  if (modifiers & ui::EF_CONTROL_DOWN) {
    parts.push_back("ctrl");
  }
  if (modifiers & ui::EF_ALT_DOWN) {
    parts.push_back("alt");
  }
  if (modifiers & ui::EF_SHIFT_DOWN) {
    parts.push_back("shift");
  }
  const std::string key = base::NumberToString(accelerator.key_code());
  parts.push_back(key);
  return base::JoinString(parts, "+");
}

std::optional<ui::Accelerator> Parse(std::string_view text) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      text, "+", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.empty()) {
    return std::nullopt;
  }
  int modifiers = 0;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    int flag = 0;
    if (parts[i] == "cmd") {
      flag = ui::EF_COMMAND_DOWN;
    } else if (parts[i] == "ctrl") {
      flag = ui::EF_CONTROL_DOWN;
    } else if (parts[i] == "alt") {
      flag = ui::EF_ALT_DOWN;
    } else if (parts[i] == "shift") {
      flag = ui::EF_SHIFT_DOWN;
    }
    if (!flag || (modifiers & flag)) {
      return std::nullopt;
    }
    modifiers |= flag;
  }
  int key = 0;
  if (!base::StringToInt(parts.back(), &key) || key <= 0 || key > 0xFF) {
    return std::nullopt;
  }
  ui::Accelerator accelerator(static_cast<ui::KeyboardCode>(key), modifiers);
  if (!IsValid(accelerator)) {
    return std::nullopt;
  }
  return accelerator;
}

}  // namespace ahoi::shortcuts
