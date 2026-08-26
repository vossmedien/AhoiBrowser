// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_APP_STARTUP_POLICY_H_
#define AHOI_APP_STARTUP_POLICY_H_

namespace base {
class CommandLine;
}

namespace ahoi::startup {

// Applies product invariants that must be present before Chromium constructs
// its FeatureList. The operation is deterministic and idempotent.
void ApplyEarlyStartupPolicy(base::CommandLine& command_line);

}  // namespace ahoi::startup

#endif  // AHOI_APP_STARTUP_POLICY_H_
