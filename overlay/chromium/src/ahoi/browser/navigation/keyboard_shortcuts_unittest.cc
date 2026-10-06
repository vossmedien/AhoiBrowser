// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/keyboard_shortcuts.h"

#include <set>
#include <vector>

#include "base/functional/bind.h"
#include "base/values.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::shortcuts {
namespace {

constexpr int kCmd = ui::EF_COMMAND_DOWN;
constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;

ui::Accelerator Key(ui::KeyboardCode key, int modifiers) {
  return ui::Accelerator(key, modifiers);
}

class KeyboardShortcutsTest : public ::testing::Test {
 public:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  ConflictSources NoSources() { return {}; }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(KeyboardShortcutsTest, CatalogIdsAreUniqueAndDefaultsDoNotCollide) {
  std::set<std::string> ids;
  for (const ShortcutCommand& command : Catalog()) {
    EXPECT_TRUE(ids.insert(command.id).second) << command.id;
    EXPECT_FALSE(command.title_de.empty()) << command.id;
    EXPECT_FALSE(command.title_en.empty()) << command.id;
    for (const ui::Accelerator& accelerator : command.defaults) {
      EXPECT_EQ(command.id, CommandForAccelerator({}, accelerator))
          << command.id;
      EXPECT_FALSE(IsReservedBySystem(accelerator)) << command.id;
    }
  }
  EXPECT_EQ(2u, IndexedCommandIndex("workspace.3", kWorkspacePrefix));
  EXPECT_EQ(0u, IndexedCommandIndex("split.focus-pane-1", kSplitPanePrefix));
  EXPECT_FALSE(IndexedCommandIndex("workspace.next", kWorkspacePrefix));
  EXPECT_FALSE(IndexedCommandIndex("workspace.0", kWorkspacePrefix));
}

TEST_F(KeyboardShortcutsTest, DefaultsKeepTodaysKeysAndAddLastUsedTab) {
  EXPECT_EQ(kSplitCycleLayout,
            CommandForAccelerator({}, Key(ui::VKEY_L, kCmd | kCtrl)));
  EXPECT_EQ("workspace.9",
            CommandForAccelerator({}, Key(ui::VKEY_9, kCtrl)));
  EXPECT_EQ(kSwitchToLastUsedTab,
            CommandForAccelerator({}, Key(ui::VKEY_TAB, kCtrl | kAlt)));
  // Chromium's own Control+Tab cycling stays outside the catalog.
  EXPECT_FALSE(CommandForAccelerator({}, Key(ui::VKEY_TAB, kCtrl)));
}

TEST_F(KeyboardShortcutsTest, RebindUnbindAndReset) {
  const ui::Accelerator new_key = Key(ui::VKEY_J, kCmd | kAlt);
  Conflict conflict;
  ASSERT_TRUE(SetBinding(&prefs_, kSwitchToLastUsedTab, new_key, NoSources(),
                         &conflict));
  EXPECT_EQ(Conflict(), conflict);
  Overrides overrides = ReadOverrides(prefs_);
  EXPECT_EQ(kSwitchToLastUsedTab, CommandForAccelerator(overrides, new_key));
  EXPECT_FALSE(
      CommandForAccelerator(overrides, Key(ui::VKEY_TAB, kCtrl | kAlt)));

  ASSERT_TRUE(Unbind(&prefs_, kSwitchToLastUsedTab));
  overrides = ReadOverrides(prefs_);
  EXPECT_TRUE(EffectiveAccelerators(overrides, kSwitchToLastUsedTab).empty());
  EXPECT_FALSE(CommandForAccelerator(overrides, new_key));

  ASSERT_TRUE(ResetToDefault(&prefs_, kSwitchToLastUsedTab));
  EXPECT_EQ(kSwitchToLastUsedTab,
            CommandForAccelerator(ReadOverrides(prefs_),
                                  Key(ui::VKEY_TAB, kCtrl | kAlt)));
}

TEST_F(KeyboardShortcutsTest, ConflictsAreReportedAndNeverOverwritten) {
  const auto is_key = [](ui::Accelerator wanted, const ui::Accelerator& a) {
    return a == wanted;
  };
  ConflictSources sources{
      .is_browser_accelerator =
          base::BindRepeating(is_key, Key(ui::VKEY_Y, kCmd)),
      .is_extension_accelerator =
          base::BindRepeating(is_key, Key(ui::VKEY_E, kCmd | kShift)),
  };
  Conflict conflict;
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace,
                          Key(ui::VKEY_L, kCmd | kCtrl), sources, &conflict));
  EXPECT_EQ((Conflict{.kind = ConflictKind::kOtherCommand,
                      .other_command_id = kSplitCycleLayout}),
            conflict);
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_Y, kCmd),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kBrowserCommand, conflict.kind);
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace,
                          Key(ui::VKEY_E, kCmd | kShift), sources, &conflict));
  EXPECT_EQ(ConflictKind::kExtension, conflict.kind);
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_Q, kCmd),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kReservedBySystem, conflict.kind);
  // macOS 27 swallows Option+Tab before any app (WORKFLOW-03).
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_TAB, kAlt),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kReservedBySystem, conflict.kind);
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_K, kAlt),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kInvalid, conflict.kind);
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_K, ui::EF_NONE),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kInvalid, conflict.kind);
  EXPECT_FALSE(SetBinding(&prefs_, "unknown.command",
                          Key(ui::VKEY_K, kCmd | kAlt), sources, &conflict));
  EXPECT_EQ(ConflictKind::kNotRebindable, conflict.kind);
  // Nothing was written by the refused attempts.
  EXPECT_TRUE(prefs_.GetDict(kShortcutBindingsPref).empty());
  // Binding a command to its own key is not a conflict.
  EXPECT_TRUE(SetBinding(&prefs_, kNextWorkspace,
                         Key(ui::VKEY_RIGHT, kCmd | kAlt), sources, &conflict));
}

TEST_F(KeyboardShortcutsTest, ResetIsRefusedWhenAnotherCommandTookTheDefault) {
  ASSERT_TRUE(Unbind(&prefs_, kSplitCycleLayout));
  ASSERT_TRUE(SetBinding(&prefs_, kNextWorkspace,
                         Key(ui::VKEY_L, kCmd | kCtrl), NoSources(), nullptr));
  EXPECT_FALSE(ResetToDefault(&prefs_, kSplitCycleLayout));
  EXPECT_TRUE(ResetToDefault(&prefs_, kNextWorkspace));
  EXPECT_TRUE(ResetToDefault(&prefs_, kSplitCycleLayout));
  ResetAll(&prefs_);
  EXPECT_TRUE(prefs_.GetDict(kShortcutBindingsPref).empty());
}

TEST_F(KeyboardShortcutsTest, SerializationRoundTripsAndRejectsDamage) {
  const ui::Accelerator key = Key(ui::VKEY_S, kCmd | kShift);
  EXPECT_EQ("cmd+shift+83", Serialize(key));
  EXPECT_EQ(key, Parse("cmd+shift+83"));
  EXPECT_FALSE(Parse(""));
  EXPECT_FALSE(Parse("cmd+cmd+83"));
  EXPECT_FALSE(Parse("hyper+83"));
  EXPECT_FALSE(Parse("cmd+999"));
  EXPECT_FALSE(Parse("83"));

  base::DictValue stored;
  stored.Set(kNextWorkspace, base::ListValue().Append("garbage"));
  stored.Set("unknown.command", base::ListValue().Append("cmd+75"));
  prefs_.SetDict(kShortcutBindingsPref, std::move(stored));
  // Damaged and unknown entries fall back to the defaults.
  EXPECT_TRUE(ReadOverrides(prefs_).empty());
}

// ⌥⌘T edits the address; ⇧⌘T stays Chromium's "Reopen closed tab".
TEST_F(KeyboardShortcutsTest, FocusAddressBarUsesOptionCommandT) {
  const ShortcutCommand* command = FindCommand(kFocusAddressBar);
  ASSERT_TRUE(command);
  EXPECT_TRUE(command->rebindable);
  EXPECT_EQ(std::vector<ui::Accelerator>{Key(ui::VKEY_T, kCmd | kAlt)},
            command->defaults);
  EXPECT_EQ(kFocusAddressBar,
            CommandForAccelerator({}, Key(ui::VKEY_T, kCmd | kAlt)));
  EXPECT_FALSE(CommandForAccelerator({}, Key(ui::VKEY_T, kCmd | kShift)));
}

TEST_F(KeyboardShortcutsTest, FormerlyFixedCommandsKeepTheirDefaults) {
  const struct {
    const char* id;
    ui::Accelerator key;
  } kDefaults[] = {
      {kCommandBar, Key(ui::VKEY_L, kCmd)},
      {kCommandBarNewTab, Key(ui::VKEY_T, kCmd)},
      {kQuickWindow, Key(ui::VKEY_SPACE, kAlt)},
      {kSaveTab, Key(ui::VKEY_D, kCmd)},
      {kSidebarUndo, Key(ui::VKEY_Z, kCmd)},
  };
  for (const auto& entry : kDefaults) {
    const ShortcutCommand* command = FindCommand(entry.id);
    ASSERT_TRUE(command) << entry.id;
    EXPECT_TRUE(command->rebindable) << entry.id;
    EXPECT_EQ(std::vector<ui::Accelerator>{entry.key}, command->defaults)
        << entry.id;
    EXPECT_EQ(entry.id, CommandForAccelerator({}, entry.key));
  }
  // The command bar lists Quick Window, Undo and Save with their keys, but
  // not itself.
  EXPECT_TRUE(ShownInCommandBar(*FindCommand(kQuickWindow)));
  EXPECT_TRUE(ShownInCommandBar(*FindCommand(kSidebarUndo)));
  EXPECT_TRUE(ShownInCommandBar(*FindCommand(kSaveTab)));
  EXPECT_FALSE(ShownInCommandBar(*FindCommand(kCommandBar)));
  EXPECT_FALSE(ShownInCommandBar(*FindCommand(kCommandBarNewTab)));
  // The sidebar's ⇧⌘S is not Save; Save stays on ⌘D.
  EXPECT_EQ(kToggleSidebarFloating,
            CommandForAccelerator({}, Key(ui::VKEY_S, kCmd | kShift)));
}

TEST_F(KeyboardShortcutsTest, FormerlyFixedCommandsRebindAndResetPerEntry) {
  const ui::Accelerator quick = Key(ui::VKEY_SPACE, kCtrl | kAlt | kShift);
  const ui::Accelerator undo = Key(ui::VKEY_Z, kCmd | kCtrl);
  const ui::Accelerator bar = Key(ui::VKEY_K, kCmd);
  const ui::Accelerator save = Key(ui::VKEY_S, kCmd | kAlt);
  Conflict conflict;
  ASSERT_TRUE(
      SetBinding(&prefs_, kQuickWindow, quick, NoSources(), &conflict));
  ASSERT_TRUE(SetBinding(&prefs_, kSidebarUndo, undo, NoSources(), &conflict));
  ASSERT_TRUE(SetBinding(&prefs_, kCommandBar, bar, NoSources(), &conflict));
  ASSERT_TRUE(SetBinding(&prefs_, kSaveTab, save, NoSources(), &conflict));
  Overrides overrides = ReadOverrides(prefs_);
  EXPECT_EQ(kQuickWindow, CommandForAccelerator(overrides, quick));
  EXPECT_EQ(kSidebarUndo, CommandForAccelerator(overrides, undo));
  EXPECT_EQ(kCommandBar, CommandForAccelerator(overrides, bar));
  EXPECT_EQ(kSaveTab, CommandForAccelerator(overrides, save));
  // The old keys are free again; ⌘T still opens the command bar for a new
  // tab because that entry was not changed.
  EXPECT_FALSE(CommandForAccelerator(overrides, Key(ui::VKEY_L, kCmd)));
  EXPECT_FALSE(CommandForAccelerator(overrides, Key(ui::VKEY_D, kCmd)));
  EXPECT_FALSE(CommandForAccelerator(overrides, Key(ui::VKEY_Z, kCmd)));
  EXPECT_FALSE(CommandForAccelerator(overrides, Key(ui::VKEY_SPACE, kAlt)));
  EXPECT_EQ(kCommandBarNewTab,
            CommandForAccelerator(overrides, Key(ui::VKEY_T, kCmd)));

  // Taking another entry's key is refused and names that entry.
  EXPECT_FALSE(SetBinding(&prefs_, kSaveTab, Key(ui::VKEY_T, kCmd),
                          NoSources(), &conflict));
  EXPECT_EQ((Conflict{.kind = ConflictKind::kOtherCommand,
                      .other_command_id = kCommandBarNewTab}),
            conflict);
  EXPECT_FALSE(
      SetBinding(&prefs_, kNextWorkspace, quick, NoSources(), &conflict));
  EXPECT_EQ(kQuickWindow, conflict.other_command_id);

  // Reset is per entry: only Quick Window goes back to ⌥Space.
  ASSERT_TRUE(ResetToDefault(&prefs_, kQuickWindow));
  overrides = ReadOverrides(prefs_);
  EXPECT_EQ(kQuickWindow,
            CommandForAccelerator(overrides, Key(ui::VKEY_SPACE, kAlt)));
  EXPECT_EQ(kSidebarUndo, CommandForAccelerator(overrides, undo));
  EXPECT_EQ(kCommandBar, CommandForAccelerator(overrides, bar));
  EXPECT_EQ(3u, overrides.size());

  // A freed default can be taken; resetting its owner is then refused.
  ASSERT_TRUE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_D, kCmd),
                         NoSources(), &conflict));
  EXPECT_FALSE(ResetToDefault(&prefs_, kSaveTab));
  EXPECT_EQ(std::vector<ui::Accelerator>{save},
            EffectiveAccelerators(ReadOverrides(prefs_), kSaveTab));
}

TEST_F(KeyboardShortcutsTest, SystemShortcutsFromSettingsAreRefused) {
  // For example Mission Control's "Switch to Desktop 1" (symbolic hotkey
  // 118) moved to ⌘1 in System Settings.
  ConflictSources sources{
      .is_system_accelerator =
          base::BindRepeating([](const ui::Accelerator& a) {
            return a == ui::Accelerator(ui::VKEY_1, ui::EF_COMMAND_DOWN);
          }),
  };
  Conflict conflict;
  EXPECT_FALSE(SetBinding(&prefs_, kQuickWindow, Key(ui::VKEY_1, kCmd),
                          sources, &conflict));
  EXPECT_EQ(ConflictKind::kReservedBySystem, conflict.kind);
  EXPECT_TRUE(prefs_.GetDict(kShortcutBindingsPref).empty());
  // Defaults are not moved because of it.
  EXPECT_EQ("workspace.1", CommandForAccelerator({}, Key(ui::VKEY_1, kCtrl)));
  EXPECT_TRUE(SetBinding(&prefs_, kQuickWindow, Key(ui::VKEY_2, kCmd | kAlt),
                         sources, &conflict));
}

TEST_F(KeyboardShortcutsTest, ManagedBindingsAreNotWritten) {
  prefs_.SetManagedPref(kShortcutBindingsPref, base::Value(base::DictValue()));
  EXPECT_FALSE(Unbind(&prefs_, kNextWorkspace));
  EXPECT_FALSE(SetBinding(&prefs_, kNextWorkspace, Key(ui::VKEY_J, kCmd),
                          NoSources(), nullptr));
}

}  // namespace
}  // namespace ahoi::shortcuts
