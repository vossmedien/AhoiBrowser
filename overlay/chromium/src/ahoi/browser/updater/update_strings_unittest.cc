// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/updater/update_strings.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::updater {
namespace {

TEST(UpdateStringsTest, CoversGermanEnglishAndBritishEnglish) {
  EXPECT_EQ("Nach Updates suchen...",
            LocalizedUpdateString(UpdateString::kCheckMenu, "de-DE"));
  EXPECT_EQ("Check for Updates...",
            LocalizedUpdateString(UpdateString::kCheckMenu, "en-US"));
  EXPECT_EQ("Check for Updates...",
            LocalizedUpdateString(UpdateString::kCheckMenu, "en-GB"));
  EXPECT_EQ("Ein Update ist verfügbar.",
            LocalizedUpdateString(UpdateString::kUpdateAvailable, "de_AT"));
}

TEST(UpdateStringsTest, FallsBackToEnglish) {
  EXPECT_EQ("Software Updates",
            LocalizedUpdateString(UpdateString::kSettingsTitle, "fr-FR"));
}

}  // namespace
}  // namespace ahoi::updater
