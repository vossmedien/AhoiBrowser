// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <string_view>

#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "ahoi/browser/ui/settings/ahoi_settings_handler.h"
#include "ahoi/browser/ui/settings/shortcut_settings_model.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/command.h"

// Defined in chrome/browser/ui/cocoa/accelerator_utils_cocoa.mm (Chromium's
// accelerator table and main-menu key equivalents). This handler is linked
// into the same browser library but must not depend on //chrome/browser/ui,
// which depends on it; the declaration matches accelerator_utils.h.
bool IsChromeAccelerator(const ui::Accelerator& accelerator);

namespace ahoi::settings {
namespace {

// chrome/common/pref_names.h `prefs::kExtensionCommands`: keys are
// "<platform>:<accelerator>" for every extension shortcut Chromium assigned.
constexpr char kExtensionCommandsPref[] = "extensions.commands";

bool HasCallbackId(const base::ListValue& args) {
  return !args.empty() && args.front().is_string() &&
         !args.front().GetString().empty();
}

bool CanWriteShortcuts(const PrefService* prefs) {
  return prefs && prefs->FindPreference(shortcuts::kShortcutBindingsPref) &&
         !prefs->IsManagedPreference(shortcuts::kShortcutBindingsPref);
}

bool IsExtensionShortcut(const PrefService* prefs,
                         const ui::Accelerator& accelerator) {
  if (!prefs || !prefs->FindPreference(kExtensionCommandsPref)) {
    return false;
  }
  const std::string wanted = ui::Command::CommandPlatform() + ":" +
                             ui::Command::AcceleratorToString(accelerator);
  for (const auto [key, value] : prefs->GetDict(kExtensionCommandsPref)) {
    // Media keys append ":<extension id>"; ordinary keys match exactly.
    if (key == wanted || base::StartsWith(key, wanted + ":")) {
      return true;
    }
  }
  return false;
}

}  // namespace

base::DictValue AhoiSettingsHandler::BuildShortcutStatus(
    std::string_view action,
    std::string_view error_label) const {
  const PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
  base::DictValue status = BuildShortcutState(
      prefs ? shortcuts::ReadOverrides(*prefs) : shortcuts::Overrides(),
      CanWriteShortcuts(prefs));
  status.Set("action", std::string(action));
  status.Set("errorLabel", std::string(error_label));
  return status;
}

void AhoiSettingsHandler::PushShortcutStatus() {
  if (!IsJavascriptAllowed() || !IsAuthorizedSettingsPage()) {
    return;
  }
  FireWebUIListener("ahoi-shortcuts-changed",
                    base::Value(BuildShortcutStatus({}, {})));
}

void AhoiSettingsHandler::HandleGetShortcuts(const base::ListValue& args) {
  if (args.size() != 1u || !HasCallbackId(args) ||
      !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  if (profile_ && !shortcut_pref_registrar_.prefs() &&
      profile_->GetPrefs()->FindPreference(shortcuts::kShortcutBindingsPref)) {
    shortcut_pref_registrar_.Init(profile_->GetPrefs());
    shortcut_pref_registrar_.Add(
        shortcuts::kShortcutBindingsPref,
        base::BindRepeating(&AhoiSettingsHandler::PushShortcutStatus,
                            base::Unretained(this)));
  }
  ResolveJavascriptCallback(args.front(),
                            base::Value(BuildShortcutStatus({}, {})));
}

void AhoiSettingsHandler::HandleShortcutAction(const base::ListValue& args) {
  if (!HasCallbackId(args) || !IsAuthorizedSettingsPage()) {
    return;
  }
  AllowJavascript();
  PrefService* prefs = profile_ ? profile_->GetPrefs() : nullptr;
  if (args.size() != 3u || !args[1].is_string() || !args[2].is_dict() ||
      !CanWriteShortcuts(prefs)) {
    ResolveJavascriptCallback(
        args.front(),
        base::Value(BuildShortcutStatus(
            "blocked", ShortcutErrorLabel({.error = "writeFailed"}))));
    return;
  }
  const shortcuts::ConflictSources sources{
      .is_browser_accelerator = base::BindRepeating(&IsChromeAccelerator),
      .is_extension_accelerator =
          base::BindRepeating(&IsExtensionShortcut, base::Unretained(prefs)),
  };
  const ShortcutActionResult result =
      ApplyShortcutAction(prefs, args[1].GetString(), args[2].GetDict(),
                          sources);
  ResolveJavascriptCallback(
      args.front(),
      base::Value(BuildShortcutStatus(result.error.empty() ? "saved" : "invalid",
                                      ShortcutErrorLabel(result))));
}

void AhoiSettingsHandler::HandleSetShortcutRecording(
    const base::ListValue& args) {
  if (args.size() != 1u || !args[0].is_bool() || !IsAuthorizedSettingsPage()) {
    return;
  }
  shortcuts::SetRecordingActive(args[0].GetBool());
}

}  // namespace ahoi::settings
