// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_SESSION_SESSION_PREFS_H_
#define AHOI_BROWSER_SESSION_SESSION_PREFS_H_

#include <optional>
#include <string_view>

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace ahoi::session {

inline constexpr char kStartupModePref[] = "ahoi.session.startup_mode";

enum class StartupMode {
  kAsk = 0,
  kContinue = 1,
  kEmpty = 2,
};

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

std::string_view StartupModeToPrefValue(StartupMode mode);
std::optional<StartupMode> StartupModeFromPrefValue(std::string_view value);

// Unknown or corrupt stored values fail back to the product default, `kAsk`.
StartupMode GetStartupMode(const PrefService& prefs);

// Returns false for managed preferences or an invalid enum value.
bool SetStartupMode(PrefService* prefs, StartupMode mode);
bool IsStartupModeManaged(const PrefService& prefs);

}  // namespace ahoi::session

#endif  // AHOI_BROWSER_SESSION_SESSION_PREFS_H_
