// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/browser_account_policy.h"

#include "base/command_line.h"
#include "base/test/scoped_command_line.h"
#include "components/sync/base/command_line_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::privacy {
namespace {

TEST(BrowserAccountPolicyTest, DisablesChromiumSyncAndBrowserSignin) {
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->RemoveSwitch(syncer::kDisableSync);
  command_line->RemoveSwitch(kAllowBrowserSigninSwitch);

  ApplyBrowserAccountPolicy(*command_line);

  EXPECT_TRUE(command_line->HasSwitch(syncer::kDisableSync));
  EXPECT_FALSE(syncer::IsSyncAllowedByFlag());
  EXPECT_EQ("false",
            command_line->GetSwitchValueASCII(kAllowBrowserSigninSwitch));
  EXPECT_FALSE(IsBrowserAccountNetworkAllowed(*command_line));
}

TEST(BrowserAccountPolicyTest, ReplacesHostileOverrideAndPreservesWebUrl) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(kAllowBrowserSigninSwitch, "true");
  command_line.AppendSwitchASCII(syncer::kDisableSync, "false");
  command_line.AppendArg("https://accounts.google.com/");
  const base::CommandLine::StringVector original_args = command_line.GetArgs();

  ApplyBrowserAccountPolicy(command_line);
  ApplyBrowserAccountPolicy(command_line);

  EXPECT_TRUE(command_line.HasSwitch(syncer::kDisableSync));
  EXPECT_EQ("false",
            command_line.GetSwitchValueASCII(kAllowBrowserSigninSwitch));
  EXPECT_FALSE(IsBrowserAccountNetworkAllowed(command_line));
  EXPECT_EQ(original_args, command_line.GetArgs());
}

TEST(BrowserAccountPolicyTest, NetworkInitializationMatchesExplicitPolicy) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  EXPECT_TRUE(IsBrowserAccountNetworkAllowed(command_line));

  command_line.AppendSwitchASCII(kAllowBrowserSigninSwitch, "true");
  EXPECT_TRUE(IsBrowserAccountNetworkAllowed(command_line));

  command_line.RemoveSwitch(kAllowBrowserSigninSwitch);
  command_line.AppendSwitchASCII(kAllowBrowserSigninSwitch, "FALSE");
  EXPECT_FALSE(IsBrowserAccountNetworkAllowed(command_line));
}

}  // namespace
}  // namespace ahoi::privacy
