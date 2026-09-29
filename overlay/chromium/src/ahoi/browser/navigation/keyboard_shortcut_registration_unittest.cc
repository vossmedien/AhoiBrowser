// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/keyboard_shortcut_registration.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ahoi::shortcuts {
namespace {

class ShortcutRegistrationTest : public ::testing::Test {
 public:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  std::unique_ptr<ShortcutRegistration> Create() {
    return std::make_unique<ShortcutRegistration>(
        &prefs_, base::BindRepeating(
                     [](std::set<ui::Accelerator>* registered,
                        int* calls, const ui::Accelerator& accelerator,
                        bool register_it) {
                       ++*calls;
                       if (register_it) {
                         EXPECT_TRUE(registered->insert(accelerator).second);
                       } else {
                         EXPECT_EQ(1u, registered->erase(accelerator));
                       }
                     },
                     &registered_, &calls_));
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
  std::set<ui::Accelerator> registered_;
  int calls_ = 0;
};

TEST_F(ShortcutRegistrationTest, RegistersEveryCatalogDefault) {
  auto registration = Create();
  const ui::Accelerator last_used(ui::VKEY_TAB,
                                  ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
  EXPECT_TRUE(registered_.contains(last_used));
  EXPECT_EQ(kSwitchToLastUsedTab, registration->CommandFor(last_used));
  // Quick Window, sidebar Undo, the command bar and Save follow the catalog
  // like every other command; their old fixed registrations are gone.
  const struct {
    ui::Accelerator key;
    const char* id;
  } kFormerlyFixed[] = {
      {ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN), kQuickWindow},
      {ui::Accelerator(ui::VKEY_Z, ui::EF_COMMAND_DOWN), kSidebarUndo},
      {ui::Accelerator(ui::VKEY_L, ui::EF_COMMAND_DOWN), kCommandBar},
      {ui::Accelerator(ui::VKEY_T, ui::EF_COMMAND_DOWN), kCommandBarNewTab},
      {ui::Accelerator(ui::VKEY_D, ui::EF_COMMAND_DOWN), kSaveTab},
  };
  for (const auto& entry : kFormerlyFixed) {
    EXPECT_TRUE(registered_.contains(entry.key)) << entry.id;
    EXPECT_EQ(entry.id, registration->CommandFor(entry.key));
  }
}

TEST_F(ShortcutRegistrationTest, ChangedCallbackRunsAfterEachBindingChange) {
  auto registration = Create();
  const ui::Accelerator new_key(ui::VKEY_K,
                                ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN);
  int changes = 0;
  std::optional<std::string> seen;
  registration->SetChangedCallback(base::BindRepeating(
      [](ShortcutRegistration* registration, ui::Accelerator key,
         int* changes, std::optional<std::string>* seen) {
        ++*changes;
        // The registration already follows the new binding.
        *seen = registration->CommandFor(key);
      },
      registration.get(), new_key, &changes, &seen));
  EXPECT_EQ(0, changes);

  ASSERT_TRUE(SetBinding(&prefs_, kQuickWindow, new_key, {}, nullptr));
  EXPECT_EQ(1, changes);
  EXPECT_EQ(kQuickWindow, seen);
  EXPECT_EQ(std::vector<ui::Accelerator>{new_key},
            EffectiveAccelerators(registration->overrides(), kQuickWindow));

  ASSERT_TRUE(ResetToDefault(&prefs_, kQuickWindow));
  EXPECT_EQ(2, changes);
  EXPECT_FALSE(seen);
}

TEST_F(ShortcutRegistrationTest, FollowsBindingChangesWithMinimalUpdates) {
  auto registration = Create();
  const size_t initial = registered_.size();
  const ui::Accelerator old_key(ui::VKEY_TAB,
                                ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
  const ui::Accelerator new_key(ui::VKEY_J,
                                ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN);
  calls_ = 0;
  ASSERT_TRUE(SetBinding(&prefs_, kSwitchToLastUsedTab, new_key, {}, nullptr));
  EXPECT_EQ(2, calls_);  // One unregister, one register.
  EXPECT_FALSE(registered_.contains(old_key));
  EXPECT_TRUE(registered_.contains(new_key));
  EXPECT_EQ(initial, registered_.size());
  EXPECT_EQ(kSwitchToLastUsedTab, registration->CommandFor(new_key));
  EXPECT_FALSE(registration->CommandFor(old_key));

  ASSERT_TRUE(Unbind(&prefs_, kSwitchToLastUsedTab));
  EXPECT_FALSE(registered_.contains(new_key));
  ResetAll(&prefs_);
  EXPECT_TRUE(registered_.contains(old_key));
  EXPECT_EQ(initial, registered_.size());
}

}  // namespace
}  // namespace ahoi::shortcuts
