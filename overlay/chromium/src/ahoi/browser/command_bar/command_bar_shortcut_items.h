// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_SHORTCUT_ITEMS_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_SHORTCUT_ITEMS_H_

#include <vector>

#include "ahoi/browser/navigation/command_service.h"
#include "ahoi/browser/navigation/keyboard_shortcuts.h"

namespace ahoi::internal {

// The shared shortcut catalog as command-bar items: every command the bar
// lists, with its current key under `overrides` as the secondary text, so
// the command bar and the keys run and show the same thing.
std::vector<CommandItem> BuildShortcutCommandItems(
    const shortcuts::Overrides& overrides);

}  // namespace ahoi::internal

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_BAR_SHORTCUT_ITEMS_H_
