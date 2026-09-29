// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/command_bar/command_bar_shortcut_items.h"

#include <string>
#include <string_view>
#include <vector>

#include "ahoi/browser/navigation/keyboard_shortcuts.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::internal {
namespace {

// The controller republishes these items whenever the bindings pref
// changes; keyboard-shortcuts-journey (commandBarShowsBinding) covers that
// wiring in the installed browser.
const CommandItem* FindItem(const std::vector<CommandItem>& items,
                            std::string_view stable_id) {
  for (const CommandItem& item : items) {
    if (item.stable_id == stable_id) {
      return &item;
    }
  }
  return nullptr;
}

TEST(CommandBarShortcutItemsTest, ShowsTheDefaultKey) {
  const std::vector<CommandItem> items =
      BuildShortcutCommandItems(shortcuts::Overrides());
  const CommandItem* save = FindItem(items, "shortcut.tab.save");
  ASSERT_TRUE(save);
  EXPECT_EQ(save->type, CommandItemType::kBrowserCommand);
  EXPECT_EQ(save->secondary_text, u"⌘D");
}

TEST(CommandBarShortcutItemsTest, ShowsTheReboundKey) {
  shortcuts::Overrides overrides;
  overrides[shortcuts::kSaveTab] = {
      ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)};
  const CommandItem* save =
      FindItem(BuildShortcutCommandItems(overrides), "shortcut.tab.save");
  ASSERT_TRUE(save);
  EXPECT_EQ(save->secondary_text, u"⌥⌘S");
}

TEST(CommandBarShortcutItemsTest, UnboundCommandHasNoKey) {
  shortcuts::Overrides overrides;
  overrides[shortcuts::kSaveTab] = {};
  const CommandItem* save =
      FindItem(BuildShortcutCommandItems(overrides), "shortcut.tab.save");
  ASSERT_TRUE(save);
  EXPECT_TRUE(save->secondary_text.empty());
}

TEST(CommandBarShortcutItemsTest, OmitsTheCommandBarItself) {
  const std::vector<CommandItem> items =
      BuildShortcutCommandItems(shortcuts::Overrides());
  EXPECT_FALSE(FindItem(items, "shortcut.browser.command-bar"));
}

}  // namespace
}  // namespace ahoi::internal
