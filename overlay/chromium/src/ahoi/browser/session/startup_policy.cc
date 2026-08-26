// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/session/startup_policy.h"

namespace ahoi::session {

StartupDisposition ResolveStartupDisposition(
    const StartupPolicyContext& context) {
  // Chromium remains authoritative for modes whose safety or user intent is
  // stronger than Ahoi's normal clean-start preference. In particular, Ahoi
  // must never turn its regular-profile continuation into OTR restoration or
  // suppress Chromium's crash recovery and managed RestoreOnStartup policy.
  if (context.is_off_the_record || context.has_managed_restore_policy ||
      context.has_explicit_command_line_intent || context.is_browser_restart ||
      context.is_tabbed_restore_suppressed || context.is_post_crash_launch ||
      context.is_first_run) {
    return StartupDisposition::kDeferToChromium;
  }

  if (!context.has_restorable_session) {
    return StartupDisposition::kStartEmpty;
  }

  switch (context.mode) {
    case StartupMode::kAsk:
      return StartupDisposition::kShowChoice;
    case StartupMode::kContinue:
      return StartupDisposition::kRestoreLastSession;
    case StartupMode::kEmpty:
      return StartupDisposition::kStartEmpty;
  }
  return StartupDisposition::kShowChoice;
}

}  // namespace ahoi::session
