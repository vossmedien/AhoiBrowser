// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_PLATFORM_MAC_H_
#define AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_PLATFORM_MAC_H_

#include <vector>

#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "base/containers/span.h"
#include "ui/base/accelerators/accelerator.h"

#if defined(__OBJC__)
@class NSDictionary;
@class NSMenu;
#endif

namespace ahoi::shortcuts {

// A main-menu item, identified by its tag (a Chromium command id), whose key
// equivalent is the binding of a catalog command.
struct MainMenuBinding {
  int menu_tag;
  const char* command_id;
};

// Sets the key equivalent of every `commandDispatch:` item in the app's main
// menu whose tag matches a binding to that command's current key, or clears
// it when the command is unbound. Chromium resolves a key's command through
// the main menu, so the key and its menu display move together and the old
// key is free again.
void ApplyMainMenuKeyEquivalents(const Overrides& overrides,
                                 base::span<const MainMenuBinding> bindings);

// True when `accelerator` is a macOS shortcut switched on in System Settings
// > Keyboard > Keyboard Shortcuts for this user (a symbolic hotkey such as
// Mission Control's "Switch to Desktop 1"). A conflict source for the editor.
bool IsEnabledSystemHotKey(const ui::Accelerator& accelerator);

#if defined(__OBJC__)
// The testable parts of the two functions above.
void ApplyKeyEquivalentsToMenu(NSMenu* menu,
                               const Overrides& overrides,
                               base::span<const MainMenuBinding> bindings);
// Reads the `AppleSymbolicHotKeys` dictionary of com.apple.symbolichotkeys:
// every enabled standard entry with a key.
std::vector<ui::Accelerator> EnabledSymbolicHotKeys(NSDictionary* hotkeys);
#endif

}  // namespace ahoi::shortcuts

#endif  // AHOI_BROWSER_NAVIGATION_KEYBOARD_SHORTCUT_PLATFORM_MAC_H_
