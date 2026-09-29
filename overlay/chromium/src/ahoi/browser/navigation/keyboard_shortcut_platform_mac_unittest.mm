// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import "ahoi/browser/navigation/keyboard_shortcut_platform_mac.h"

#import <AppKit/AppKit.h>

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::shortcuts {
namespace {

constexpr int kLocationTag = 1001;
constexpr int kNewTabTag = 1002;
constexpr MainMenuBinding kBindings[] = {
    {kLocationTag, kCommandBar},
    {kNewTabTag, kCommandBarNewTab},
};

NSMenuItem* CommandItem(int tag, NSString* key) {
  const SEL dispatch = NSSelectorFromString(@"commandDispatch:");
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Item"
                                                action:dispatch
                                         keyEquivalent:key];
  item.tag = tag;
  item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
  return item;
}

// A main menu like Chromium's: File > Open Location (⌘L), New Tab (⌘T), and
// a separator that shares the New Tab tag but runs nothing.
NSMenu* MainMenu() {
  NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
  [file addItem:CommandItem(kNewTabTag, @"t")];
  [file addItem:CommandItem(kLocationTag, @"l")];
  NSMenuItem* separator = [NSMenuItem separatorItem];
  separator.tag = kNewTabTag;
  [file addItem:separator];
  NSMenu* main_menu = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem* file_item = [[NSMenuItem alloc] initWithTitle:@"File"
                                                     action:nil
                                              keyEquivalent:@""];
  file_item.submenu = file;
  [main_menu addItem:file_item];
  return main_menu;
}

NSMenuItem* Item(NSMenu* main_menu, int tag) {
  for (NSMenuItem* item in main_menu.itemArray.firstObject.submenu.itemArray) {
    if (item.tag == tag && !item.isSeparatorItem) {
      return item;
    }
  }
  return nil;
}

TEST(KeyboardShortcutPlatformMacTest, MenuKeysFollowTheBindings) {
  NSMenu* main_menu = MainMenu();
  Overrides overrides;
  overrides[kCommandBar] = {
      ui::Accelerator(ui::VKEY_K, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)};
  overrides[kCommandBarNewTab] = {};
  ApplyKeyEquivalentsToMenu(main_menu, overrides, kBindings);

  NSMenuItem* location = Item(main_menu, kLocationTag);
  EXPECT_NSEQ(@"k", location.keyEquivalent.lowercaseString);
  EXPECT_TRUE(location.keyEquivalentModifierMask & NSEventModifierFlagCommand);
  // Unbound: the item keeps its title but no longer claims ⌘T.
  EXPECT_NSEQ(@"", Item(main_menu, kNewTabTag).keyEquivalent);

  // Back to the defaults.
  ApplyKeyEquivalentsToMenu(main_menu, {}, kBindings);
  EXPECT_NSEQ(@"l", location.keyEquivalent.lowercaseString);
  EXPECT_EQ(NSEventModifierFlagCommand, location.keyEquivalentModifierMask);
  EXPECT_NSEQ(@"t", Item(main_menu, kNewTabTag).keyEquivalent.lowercaseString);
}

TEST(KeyboardShortcutPlatformMacTest, ReadsEnabledSymbolicHotKeysOnly) {
  // 118 "Switch to Desktop 1" moved to ⌘1 (key code 18), 60 "Select the
  // previous input source" (⌃Space, key code 49) switched off, 164 without a
  // key, and a mouse-button entry.
  NSDictionary* hotkeys = @{
    @"118" : @{
      @"enabled" : @YES,
      @"value" : @{
        @"parameters" : @[ @49, @18, @(NSEventModifierFlagCommand) ],
        @"type" : @"standard"
      }
    },
    @"60" : @{
      @"enabled" : @NO,
      @"value" : @{
        @"parameters" : @[ @32, @49, @(NSEventModifierFlagControl) ],
        @"type" : @"standard"
      }
    },
    @"164" : @{
      @"enabled" : @YES,
      @"value" : @{
        @"parameters" : @[ @65535, @65535, @0 ],
        @"type" : @"standard"
      }
    },
    @"1000" : @{
      @"enabled" : @YES,
      @"value" : @{@"parameters" : @[ @2, @2, @0 ], @"type" : @"button"}
    },
  };
  const std::vector<ui::Accelerator> enabled = EnabledSymbolicHotKeys(hotkeys);
  ASSERT_EQ(1u, enabled.size());
  EXPECT_EQ(ui::Accelerator(ui::VKEY_1, ui::EF_COMMAND_DOWN), enabled[0]);
  EXPECT_TRUE(EnabledSymbolicHotKeys(nil).empty());
}

}  // namespace
}  // namespace ahoi::shortcuts
