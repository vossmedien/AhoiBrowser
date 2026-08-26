// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_
#define AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_

#include <optional>
#include <string_view>

#include "ahoi/browser/developer_toolkit/developer_toolkit_types.h"

namespace ahoi::internal {

// Deliberately outside Chromium's positive command-id range. Production code
// handles this reviewed Ahoi action explicitly instead of forwarding it to
// chrome::ExecuteCommand().
inline constexpr int kOpenInNormalWindowCommand = -1;
inline constexpr int kOpenPrivacyModeCommand = -2;
inline constexpr int kSwitchHttpAuthAccountCommand = -3;
inline constexpr int kForgetHttpAuthRealmCommand = -4;
inline constexpr int kManageHttpAuthCredentialsCommand = -5;

// Converts only the deliberately small, reviewed command-bar allowlist into
// Chromium command identifiers. Keeping this in the testable core prevents a
// string supplied by an index publisher from becoming an arbitrary browser
// command.
std::optional<int> GetAllowlistedBrowserCommand(std::string_view stable_id);
std::optional<DeveloperAction> GetAllowlistedDeveloperAction(
    std::string_view stable_id);

}  // namespace ahoi::internal

#endif  // AHOI_BROWSER_COMMAND_BAR_COMMAND_EXECUTION_ADAPTER_INTERNAL_H_
