// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SETTINGS_SHORTCUT_SETTINGS_MODEL_H_
#define AHOI_BROWSER_UI_SETTINGS_SHORTCUT_SETTINGS_MODEL_H_

#include <string>
#include <string_view>

#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "base/values.h"
#include "ui/base/accelerators/accelerator.h"

class PrefService;

namespace ahoi::settings {

// WebUI model of the keyboard shortcut editor: the shared catalog with the
// current and default keys of every command, and the editor's actions.
base::DictValue BuildShortcutState(const shortcuts::Overrides& overrides,
                                   bool can_change);

struct ShortcutActionResult {
  // Empty on success, otherwise a stable code ("conflict", "invalidRequest",
  // "unknownCommand", "writeFailed").
  std::string error;
  shortcuts::Conflict conflict;
};

// Actions: "set" {id, keyCode, cmd, ctrl, alt, shift}, "unbind" {id},
// "reset" {id}, "resetAll" {}. Nothing is written unless it succeeds.
ShortcutActionResult ApplyShortcutAction(
    PrefService* prefs,
    std::string_view action,
    const base::DictValue& payload,
    const shortcuts::ConflictSources& sources);

// A sentence for the editor, naming the command that holds a key.
std::string ShortcutErrorLabel(const ShortcutActionResult& result);

}  // namespace ahoi::settings

#endif  // AHOI_BROWSER_UI_SETTINGS_SHORTCUT_SETTINGS_MODEL_H_
