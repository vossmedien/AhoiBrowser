// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_user_agent_presets.h"

#include <array>

namespace ahoi {
namespace {

constexpr char kSafariMacUserAgent[] =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15";
constexpr char kFirefoxMacUserAgent[] =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:142.0) "
    "Gecko/20100101 Firefox/142.0";
constexpr char kMobileSafariUserAgent[] =
    "Mozilla/5.0 (iPhone; CPU iPhone OS 26_0 like Mac OS X) "
    "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 "
    "Mobile/15E148 Safari/604.1";

std::string ChromeWindowsUserAgent(std::string_view current) {
  std::string result(current);
  const size_t open = result.find('(');
  const size_t close = open == std::string::npos ? std::string::npos
                                                 : result.find(')', open + 1);
  if (open == std::string::npos || close == std::string::npos) {
    return result;
  }
  result.replace(open + 1, close - open - 1, "Windows NT 10.0; Win64; x64");
  return result;
}

}  // namespace

std::optional<std::string> ResolveDeveloperUserAgentPreset(
    DeveloperUserAgentPreset preset,
    std::string_view current_browser_user_agent) {
  switch (preset) {
    case DeveloperUserAgentPreset::kBrowserDefault:
    case DeveloperUserAgentPreset::kCustom:
      return std::nullopt;
    case DeveloperUserAgentPreset::kChromeMac:
      return std::string(current_browser_user_agent);
    case DeveloperUserAgentPreset::kSafariMac:
      return std::string(kSafariMacUserAgent);
    case DeveloperUserAgentPreset::kChromeWindows:
      return ChromeWindowsUserAgent(current_browser_user_agent);
    case DeveloperUserAgentPreset::kFirefoxMac:
      return std::string(kFirefoxMacUserAgent);
    case DeveloperUserAgentPreset::kMobileSafari:
      return std::string(kMobileSafariUserAgent);
  }
  return std::nullopt;
}

DeveloperUserAgentPreset MatchDeveloperUserAgentPreset(
    std::string_view user_agent,
    std::string_view current_browser_user_agent) {
  if (user_agent.empty()) {
    return DeveloperUserAgentPreset::kBrowserDefault;
  }
  constexpr std::array<DeveloperUserAgentPreset, 5> kConcretePresets = {
      DeveloperUserAgentPreset::kChromeMac,
      DeveloperUserAgentPreset::kSafariMac,
      DeveloperUserAgentPreset::kChromeWindows,
      DeveloperUserAgentPreset::kFirefoxMac,
      DeveloperUserAgentPreset::kMobileSafari,
  };
  for (DeveloperUserAgentPreset preset : kConcretePresets) {
    if (ResolveDeveloperUserAgentPreset(preset, current_browser_user_agent) ==
        user_agent) {
      return preset;
    }
  }
  return DeveloperUserAgentPreset::kCustom;
}

}  // namespace ahoi
