// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/startup_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::session {

namespace {

StartupPolicyContext RestorableContext(StartupMode mode) {
  return StartupPolicyContext{
      .mode = mode,
      .has_restorable_session = true,
  };
}

TEST(StartupPolicyTest, AppliesConfiguredModeForCleanRegularStartup) {
  EXPECT_EQ(StartupDisposition::kShowChoice,
            ResolveStartupDisposition(RestorableContext(StartupMode::kAsk)));
  EXPECT_EQ(
      StartupDisposition::kRestoreLastSession,
      ResolveStartupDisposition(RestorableContext(StartupMode::kContinue)));
  EXPECT_EQ(StartupDisposition::kStartEmpty,
            ResolveStartupDisposition(RestorableContext(StartupMode::kEmpty)));
}

TEST(StartupPolicyTest, NoRestorableSessionSkipsChoice) {
  EXPECT_EQ(StartupDisposition::kStartEmpty,
            ResolveStartupDisposition(StartupPolicyContext{
                .mode = StartupMode::kAsk,
                .has_restorable_session = false,
            }));
}

TEST(StartupPolicyTest, OffTheRecordAlwaysDefersToChromium) {
  StartupPolicyContext context = RestorableContext(StartupMode::kContinue);
  context.is_off_the_record = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, ManagedRestorePolicyWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kEmpty);
  context.has_managed_restore_policy = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, ExplicitCommandLineIntentWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kEmpty);
  context.has_explicit_command_line_intent = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, BrowserRestartWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kEmpty);
  context.is_browser_restart = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, SuppressedTabbedRestoreWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kContinue);
  context.is_tabbed_restore_suppressed = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, CrashRecoveryWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kEmpty);
  context.is_post_crash_launch = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

TEST(StartupPolicyTest, FirstRunWins) {
  StartupPolicyContext context = RestorableContext(StartupMode::kContinue);
  context.is_first_run = true;
  EXPECT_EQ(StartupDisposition::kDeferToChromium,
            ResolveStartupDisposition(context));
}

}  // namespace

}  // namespace ahoi::session
