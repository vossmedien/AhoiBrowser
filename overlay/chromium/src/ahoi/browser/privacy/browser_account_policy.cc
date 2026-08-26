// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/privacy/browser_account_policy.h"

#include "base/command_line.h"
#include "base/strings/string_util.h"
#include "components/sync/base/command_line_switches.h"

namespace ahoi::privacy {

void ApplyBrowserAccountPolicy(base::CommandLine& command_line) {
  // Remove first so a caller-supplied value cannot win through duplicate
  // switches. Chromium treats kDisableSync as a presence-only switch.
  command_line.RemoveSwitch(syncer::kDisableSync);
  command_line.AppendSwitch(syncer::kDisableSync);

  command_line.RemoveSwitch(kAllowBrowserSigninSwitch);
  command_line.AppendSwitchASCII(kAllowBrowserSigninSwitch, "false");
}

bool IsBrowserAccountNetworkAllowed(const base::CommandLine& command_line) {
  if (!command_line.HasSwitch(kAllowBrowserSigninSwitch)) {
    return true;
  }
  return base::EqualsCaseInsensitiveASCII(
      command_line.GetSwitchValueASCII(kAllowBrowserSigninSwitch), "true");
}

}  // namespace ahoi::privacy
