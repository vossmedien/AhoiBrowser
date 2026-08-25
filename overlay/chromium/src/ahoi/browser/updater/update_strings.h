// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UPDATER_UPDATE_STRINGS_H_
#define AHOI_BROWSER_UPDATER_UPDATE_STRINGS_H_

#include <string_view>

namespace ahoi::updater {

enum class UpdateString {
  kCheckMenu,
  kSettingsMenu,
  kSettingsTitle,
  kStatusLabel,
  kChannelLabel,
  kAutomaticChecks,
  kAutomaticDownloads,
  kCheckNow,
  kDone,
  kUnavailable,
  kIdle,
  kChecking,
  kUpdateAvailable,
  kDownloading,
  kDownloaded,
  kInstalling,
  kRelaunching,
  kUpToDate,
  kError,
  kSecurityHelp,
  kCount,
};

// The product has explicit de/en/en-GB coverage. Unsupported or malformed
// locale identifiers deliberately fall back to English.
std::string_view LocalizedUpdateString(UpdateString key,
                                       std::string_view locale);

}  // namespace ahoi::updater

#endif  // AHOI_BROWSER_UPDATER_UPDATE_STRINGS_H_
