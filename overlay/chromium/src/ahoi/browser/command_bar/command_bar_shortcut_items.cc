// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_shortcut_items.h"

#include <string>

#include "ahoi/browser/command_bar/command_execution_adapter_internal.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/accelerators/accelerator.h"

namespace ahoi::internal {

std::vector<CommandItem> BuildShortcutCommandItems(
    const shortcuts::Overrides& overrides) {
  std::vector<CommandItem> items;
  for (const shortcuts::ShortcutCommand& command : shortcuts::Catalog()) {
    if (!shortcuts::ShownInCommandBar(command)) {
      continue;
    }
    const std::vector<ui::Accelerator> keys =
        shortcuts::EffectiveAccelerators(overrides, command.id);
    items.push_back({
        .type = CommandItemType::kBrowserCommand,
        .stable_id = base::StrCat({kShortcutCommandPrefix, command.id}),
        .title = shortcuts::CommandTitle(command),
        .secondary_text =
            keys.empty()
                ? std::u16string()
                : base::UTF8ToUTF16(shortcuts::ShortcutKeyText(keys.front())),
        .keywords = {command.title_de, command.title_en},
        .priority = 150,
    });
  }
  return items;
}

}  // namespace ahoi::internal
