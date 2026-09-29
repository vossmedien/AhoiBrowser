// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/settings/shortcut_settings_model.h"

#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::settings {
namespace {

class ShortcutSettingsModelTest : public ::testing::Test {
 public:
  void SetUp() override {
    base::i18n::SetICUDefaultLocale("en_US");
    shortcuts::RegisterProfilePrefs(prefs_.registry());
  }

 protected:
  const base::DictValue* FindCommand(const base::DictValue& state,
                                     std::string_view id) {
    for (const base::Value& item : *state.FindList("commands")) {
      if (*item.GetDict().FindString("id") == id) {
        return &item.GetDict();
      }
    }
    return nullptr;
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(ShortcutSettingsModelTest, ShowsEveryCatalogCommandWithItsKeys) {
  const base::DictValue state = BuildShortcutState({}, true);
  EXPECT_EQ(shortcuts::Catalog().size(), state.FindList("commands")->size());
  const base::DictValue* last_used =
      FindCommand(state, shortcuts::kSwitchToLastUsedTab);
  ASSERT_TRUE(last_used);
  EXPECT_EQ("⌃⌥⇥", (*last_used->FindList("keys"))[0].GetString());
  EXPECT_EQ("tab", *last_used->FindString("category"));
  EXPECT_FALSE(*last_used->FindBool("customized"));
  EXPECT_TRUE(*last_used->FindBool("rebindable"));
  const base::DictValue* quick = FindCommand(state, shortcuts::kQuickWindow);
  ASSERT_TRUE(quick);
  EXPECT_TRUE(*quick->FindBool("rebindable"));
  EXPECT_EQ("⌥Space", (*quick->FindList("keys"))[0].GetString());
  EXPECT_FALSE(quick->FindString("hint")->empty());
  EXPECT_TRUE(FindCommand(state, shortcuts::kNextWorkspace)
                  ->FindString("hint")
                  ->empty());
  EXPECT_TRUE(*state.FindBool("canChange"));
}

TEST_F(ShortcutSettingsModelTest, FormerlyFixedCommandsChangeInTheEditor) {
  const std::string_view kIds[] = {
      shortcuts::kQuickWindow, shortcuts::kSidebarUndo,
      shortcuts::kCommandBar, shortcuts::kCommandBarNewTab,
      shortcuts::kSaveTab};
  const base::DictValue defaults = BuildShortcutState({}, true);
  for (std::string_view id : kIds) {
    const base::DictValue* item = FindCommand(defaults, id);
    ASSERT_TRUE(item) << id;
    EXPECT_TRUE(*item->FindBool("rebindable")) << id;
    EXPECT_EQ(1u, item->FindList("keys")->size()) << id;
  }
  EXPECT_EQ("⌘L", (*FindCommand(defaults, shortcuts::kCommandBar)
                        ->FindList("keys"))[0]
                      .GetString());
  EXPECT_EQ("⌘T", (*FindCommand(defaults, shortcuts::kCommandBarNewTab)
                        ->FindList("keys"))[0]
                      .GetString());
  EXPECT_EQ("⌘D", (*FindCommand(defaults, shortcuts::kSaveTab)
                        ->FindList("keys"))[0]
                      .GetString());
  EXPECT_EQ("⌘Z", (*FindCommand(defaults, shortcuts::kSidebarUndo)
                        ->FindList("keys"))[0]
                      .GetString());

  // Save moves to ⌥⌘S; Quick Window may not take a system key.
  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "set",
                                  base::DictValue()
                                      .Set("id", shortcuts::kSaveTab)
                                      .Set("keyCode", ui::VKEY_S)
                                      .Set("cmd", true)
                                      .Set("alt", true),
                                  {})
                  .error.empty());
  const ShortcutActionResult system = ApplyShortcutAction(
      &prefs_, "set",
      base::DictValue()
          .Set("id", shortcuts::kQuickWindow)
          .Set("keyCode", ui::VKEY_1)
          .Set("cmd", true),
      {.is_system_accelerator = base::BindRepeating(
           [](const ui::Accelerator&) { return true; })});
  EXPECT_EQ("conflict", system.error);
  EXPECT_EQ(shortcuts::ConflictKind::kReservedBySystem, system.conflict.kind);
  EXPECT_NE(std::string::npos,
            ShortcutErrorLabel(system).find("System Settings"));
  // The command bar may not take Save's new key either.
  const ShortcutActionResult taken = ApplyShortcutAction(
      &prefs_, "set",
      base::DictValue()
          .Set("id", shortcuts::kCommandBar)
          .Set("keyCode", ui::VKEY_S)
          .Set("cmd", true)
          .Set("alt", true),
      {});
  EXPECT_NE(std::string::npos, ShortcutErrorLabel(taken).find("Save tab"));

  const base::DictValue state =
      BuildShortcutState(shortcuts::ReadOverrides(prefs_), true);
  const base::DictValue* save = FindCommand(state, shortcuts::kSaveTab);
  EXPECT_EQ("⌥⌘S", (*save->FindList("keys"))[0].GetString());
  EXPECT_TRUE(*save->FindBool("customized"));
  EXPECT_EQ("⌘D", (*save->FindList("defaultKeys"))[0].GetString());
  EXPECT_FALSE(
      *FindCommand(state, shortcuts::kQuickWindow)->FindBool("customized"));

  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "reset",
                                  base::DictValue().Set(
                                      "id", shortcuts::kSaveTab),
                                  {})
                  .error.empty());
  EXPECT_TRUE(prefs_.GetDict(shortcuts::kShortcutBindingsPref).empty());
}

TEST_F(ShortcutSettingsModelTest, KeyTextUsesMacOrder) {
  EXPECT_EQ("⌃⌥⇧⌘L",
            shortcuts::ShortcutKeyText(ui::Accelerator(
                ui::VKEY_L, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
                                ui::EF_SHIFT_DOWN | ui::EF_COMMAND_DOWN)));
  EXPECT_EQ("⌘→", shortcuts::ShortcutKeyText(ui::Accelerator(ui::VKEY_RIGHT,
                                                  ui::EF_COMMAND_DOWN)));
  EXPECT_EQ("F5", shortcuts::ShortcutKeyText(ui::Accelerator(ui::VKEY_F5, 0)));
}

TEST_F(ShortcutSettingsModelTest, SetUnbindResetAndConflictMessages) {
  base::DictValue set;
  set.Set("id", shortcuts::kSwitchToLastUsedTab);
  set.Set("keyCode", ui::VKEY_K);
  set.Set("cmd", true);
  set.Set("alt", true);
  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "set", set, {}).error.empty());
  const base::DictValue state =
      BuildShortcutState(shortcuts::ReadOverrides(prefs_), true);
  const base::DictValue* item =
      FindCommand(state, shortcuts::kSwitchToLastUsedTab);
  ASSERT_TRUE(item);
  EXPECT_EQ("⌥⌘K", (*item->FindList("keys"))[0].GetString());
  EXPECT_TRUE(*item->FindBool("customized"));

  base::DictValue taken;
  taken.Set("id", shortcuts::kNextWorkspace);
  taken.Set("keyCode", ui::VKEY_K);
  taken.Set("cmd", true);
  taken.Set("alt", true);
  const ShortcutActionResult conflict =
      ApplyShortcutAction(&prefs_, "set", taken, {});
  EXPECT_EQ("conflict", conflict.error);
  EXPECT_EQ(shortcuts::kSwitchToLastUsedTab,
            conflict.conflict.other_command_id);
  EXPECT_NE(std::string::npos,
            ShortcutErrorLabel(conflict).find("Switch to the last used tab"));

  const ShortcutActionResult extension = ApplyShortcutAction(
      &prefs_, "set",
      base::DictValue()
          .Set("id", shortcuts::kNextWorkspace)
          .Set("keyCode", ui::VKEY_E)
          .Set("cmd", true)
          .Set("shift", true),
      {.is_extension_accelerator = base::BindRepeating(
           [](const ui::Accelerator&) { return true; })});
  EXPECT_EQ(shortcuts::ConflictKind::kExtension, extension.conflict.kind);

  base::DictValue id_only;
  id_only.Set("id", shortcuts::kSwitchToLastUsedTab);
  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "unbind", id_only, {}).error.empty());
  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "reset", id_only, {}).error.empty());
  EXPECT_TRUE(ApplyShortcutAction(&prefs_, "resetAll", {}, {}).error.empty());
  EXPECT_TRUE(prefs_.GetDict(shortcuts::kShortcutBindingsPref).empty());
}

TEST_F(ShortcutSettingsModelTest, RejectsMalformedRequests) {
  EXPECT_EQ("invalidRequest",
            ApplyShortcutAction(&prefs_, "set", {}, {}).error);
  EXPECT_EQ("unknownCommand",
            ApplyShortcutAction(&prefs_, "unbind",
                                base::DictValue().Set("id", "nope"), {})
                .error);
  EXPECT_EQ("invalidRequest",
            ApplyShortcutAction(&prefs_, "set",
                                base::DictValue()
                                    .Set("id", shortcuts::kNextWorkspace)
                                    .Set("keyCode", 999),
                                {})
                .error);
  EXPECT_EQ("invalidRequest",
            ApplyShortcutAction(&prefs_, "explode",
                                base::DictValue().Set(
                                    "id", shortcuts::kNextWorkspace),
                                {})
                .error);
  EXPECT_TRUE(prefs_.GetDict(shortcuts::kShortcutBindingsPref).empty());
}

}  // namespace
}  // namespace ahoi::settings
