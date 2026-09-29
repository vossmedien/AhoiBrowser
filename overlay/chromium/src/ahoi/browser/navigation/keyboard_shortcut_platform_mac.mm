// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#import "ahoi/browser/navigation/keyboard_shortcut_platform_mac.h"

#import <AppKit/AppKit.h>

#include <algorithm>

#include "base/apple/foundation_util.h"
#import "ui/base/accelerators/platform_accelerator_cocoa.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::shortcuts {

namespace {

// com.apple.symbolichotkeys stores 65535 for an entry without a key.
constexpr NSInteger kNoKeyCode = 0xFFFF;

bool SameKey(const ui::Accelerator& a, const ui::Accelerator& b) {
  constexpr int kMask = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN;
  return a.key_code() == b.key_code() &&
         (a.modifiers() & kMask) == (b.modifiers() & kMask);
}

// Only keys AppKit can show as a key equivalent; others clear the item.
bool HasKeyEquivalent(const ui::Accelerator& accelerator) {
  unichar shifted = 0;
  unichar character = 0;
  return ui::MacKeyCodeForWindowsKeyCode(accelerator.key_code(), 0, &shifted,
                                         &character) != -1;
}

void SetKeyEquivalent(NSMenuItem* item,
                      const std::vector<ui::Accelerator>& keys) {
  if (keys.empty() || !HasKeyEquivalent(keys.front())) {
    item.keyEquivalent = @"";
    item.keyEquivalentModifierMask = 0;
    return;
  }
  KeyEquivalentAndModifierMask* equivalent =
      ui::GetKeyEquivalentAndModifierMaskFromAccelerator(keys.front());
  item.keyEquivalent = equivalent.keyEquivalent ?: @"";
  item.keyEquivalentModifierMask = equivalent.modifierMask;
}

}  // namespace

void ApplyKeyEquivalentsToMenu(NSMenu* menu,
                               const Overrides& overrides,
                               base::span<const MainMenuBinding> bindings) {
  const SEL dispatch = NSSelectorFromString(@"commandDispatch:");
  for (NSMenuItem* item in menu.itemArray) {
    if (item.hasSubmenu) {
      ApplyKeyEquivalentsToMenu(item.submenu, overrides, bindings);
      continue;
    }
    // Separators share tags with commands (the Bookmarks menu); only items
    // that run a command take its key.
    if (item.action != dispatch) {
      continue;
    }
    for (const MainMenuBinding& binding : bindings) {
      if (item.tag == binding.menu_tag) {
        SetKeyEquivalent(item,
                         EffectiveAccelerators(overrides, binding.command_id));
      }
    }
  }
}

void ApplyMainMenuKeyEquivalents(const Overrides& overrides,
                                 base::span<const MainMenuBinding> bindings) {
  if (NSMenu* main_menu = NSApp.mainMenu) {
    ApplyKeyEquivalentsToMenu(main_menu, overrides, bindings);
  }
}

std::vector<ui::Accelerator> EnabledSymbolicHotKeys(NSDictionary* hotkeys) {
  std::vector<ui::Accelerator> accelerators;
  for (id hotkey_id in hotkeys) {
    NSDictionary* entry =
        base::apple::ObjCCast<NSDictionary>(hotkeys[hotkey_id]);
    NSNumber* enabled = base::apple::ObjCCast<NSNumber>(entry[@"enabled"]);
    if (!enabled.boolValue) {
      continue;
    }
    NSDictionary* value = base::apple::ObjCCast<NSDictionary>(entry[@"value"]);
    NSString* type = base::apple::ObjCCast<NSString>(value[@"type"]);
    NSArray* parameters = base::apple::ObjCCast<NSArray>(value[@"parameters"]);
    if (![type isEqualToString:@"standard"] || parameters.count < 3) {
      continue;
    }
    NSNumber* key_code = base::apple::ObjCCast<NSNumber>(parameters[1]);
    NSNumber* flags = base::apple::ObjCCast<NSNumber>(parameters[2]);
    if (!key_code || !flags || key_code.integerValue < 0 ||
        key_code.integerValue >= kNoKeyCode) {
      continue;
    }
    const ui::KeyboardCode key =
        ui::KeyboardCodeFromKeyCode(key_code.unsignedShortValue);
    if (key == ui::VKEY_UNKNOWN) {
      continue;
    }
    const NSUInteger mask = flags.unsignedIntegerValue;
    int modifiers = 0;
    if (mask & NSEventModifierFlagShift) {
      modifiers |= ui::EF_SHIFT_DOWN;
    }
    if (mask & NSEventModifierFlagControl) {
      modifiers |= ui::EF_CONTROL_DOWN;
    }
    if (mask & NSEventModifierFlagOption) {
      modifiers |= ui::EF_ALT_DOWN;
    }
    if (mask & NSEventModifierFlagCommand) {
      modifiers |= ui::EF_COMMAND_DOWN;
    }
    accelerators.emplace_back(key, modifiers);
  }
  return accelerators;
}

bool IsEnabledSystemHotKey(const ui::Accelerator& accelerator) {
  NSDictionary* domain = [NSUserDefaults.standardUserDefaults
      persistentDomainForName:@"com.apple.symbolichotkeys"];
  NSDictionary* hotkeys =
      base::apple::ObjCCast<NSDictionary>(domain[@"AppleSymbolicHotKeys"]);
  const std::vector<ui::Accelerator> enabled = EnabledSymbolicHotKeys(hotkeys);
  return std::ranges::any_of(enabled, [&](const ui::Accelerator& system) {
    return SameKey(system, accelerator);
  });
}

}  // namespace ahoi::shortcuts
