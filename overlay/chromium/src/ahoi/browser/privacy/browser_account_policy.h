// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_BROWSER_ACCOUNT_POLICY_H_
#define AHOI_BROWSER_PRIVACY_BROWSER_ACCOUNT_POLICY_H_

namespace base {
class CommandLine;
}

namespace ahoi::privacy {

inline constexpr char kAllowBrowserSigninSwitch[] = "allow-browser-signin";

// Enforces AhoiBrowser's separation between ordinary website authentication
// and browser-account services. This disables Chromium Sync and Chrome/DICE
// browser sign-in without changing navigation, cookies, or site sign-in.
void ApplyBrowserAccountPolicy(base::CommandLine& command_line);

// Returns false when Ahoi's process policy disables browser-account network
// initialization. A missing switch preserves upstream behavior for isolated
// Chromium tests; the real product always applies the explicit false value.
bool IsBrowserAccountNetworkAllowed(const base::CommandLine& command_line);

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_BROWSER_ACCOUNT_POLICY_H_
