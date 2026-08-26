// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_STARTUP_POLICY_H_
#define AHOI_BROWSER_SESSION_STARTUP_POLICY_H_

#include "ahoi/browser/session/session_prefs.h"

namespace ahoi::session {

// Inputs are deliberately free of Chromium UI types so startup precedence can
// be exhaustively tested independently from StartupBrowserCreator.
struct StartupPolicyContext {
  StartupMode mode = StartupMode::kAsk;
  bool is_off_the_record = false;
  bool has_managed_restore_policy = false;
  bool has_explicit_command_line_intent = false;
  bool is_tabbed_restore_suppressed = false;
  bool is_browser_restart = false;
  bool is_post_crash_launch = false;
  bool is_first_run = false;
  bool has_restorable_session = false;
};

enum class StartupDisposition {
  // Ahoi must not override Chromium's higher-priority startup behavior.
  kDeferToChromium = 0,
  kShowChoice = 1,
  kRestoreLastSession = 2,
  kStartEmpty = 3,
};

StartupDisposition ResolveStartupDisposition(
    const StartupPolicyContext& context);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_STARTUP_POLICY_H_
