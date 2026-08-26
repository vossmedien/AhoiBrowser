// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_USER_AGENT_PRESETS_H_
#define AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_USER_AGENT_PRESETS_H_

#include <optional>
#include <string>
#include <string_view>

namespace ahoi {

enum class DeveloperUserAgentPreset {
  kBrowserDefault,
  kChromeMac,
  kSafariMac,
  kChromeWindows,
  kFirefoxMac,
  kMobileSafari,
  kCustom,
};

// Returns a concrete override for built-in presets. Browser default and Custom
// deliberately return nullopt: default means no override, while the custom
// value stays user-owned in DeveloperProfile.
std::optional<std::string> ResolveDeveloperUserAgentPreset(
    DeveloperUserAgentPreset preset,
    std::string_view current_browser_user_agent);

DeveloperUserAgentPreset MatchDeveloperUserAgentPreset(
    std::string_view user_agent,
    std::string_view current_browser_user_agent);

}  // namespace ahoi

#endif  // AHOI_BROWSER_DEVELOPER_TOOLKIT_DEVELOPER_USER_AGENT_PRESETS_H_
