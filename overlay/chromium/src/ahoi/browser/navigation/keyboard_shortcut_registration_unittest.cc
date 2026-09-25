// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/navigation/keyboard_shortcut_registration.h"

#include <set>
#include <utility>

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

TEST_F(ShortcutRegistrationTest, RegistersRebindableDefaultsOnly) {
  auto registration = Create();
  const ui::Accelerator last_used(ui::VKEY_TAB, ui::EF_ALT_DOWN);
  EXPECT_TRUE(registered_.contains(last_used));
  // Fixed commands keep their own registration in BrowserView.
  EXPECT_FALSE(registered_.contains(
      ui::Accelerator(ui::VKEY_SPACE, ui::EF_ALT_DOWN)));
  EXPECT_FALSE(
      registered_.contains(ui::Accelerator(ui::VKEY_Z, ui::EF_COMMAND_DOWN)));
  EXPECT_EQ(kSwitchToLastUsedTab, registration->CommandFor(last_used));
  EXPECT_FALSE(registration->CommandFor(
      ui::Accelerator(ui::VKEY_L, ui::EF_COMMAND_DOWN)));
}

TEST_F(ShortcutRegistrationTest, FollowsBindingChangesWithMinimalUpdates) {
  auto registration = Create();
  const size_t initial = registered_.size();
  const ui::Accelerator old_key(ui::VKEY_TAB, ui::EF_ALT_DOWN);
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
