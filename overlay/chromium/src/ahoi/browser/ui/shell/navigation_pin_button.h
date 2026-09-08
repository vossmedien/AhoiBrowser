// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_SHELL_NAVIGATION_PIN_BUTTON_H_
#define AHOI_BROWSER_UI_SHELL_NAVIGATION_PIN_BUTTON_H_

#include <memory>

class PrefService;
namespace views {
class View;
}

namespace ahoi {

// Uses the existing floating-navigation preference and Chromium ToolbarButton
// presentation. Returns null when the profile has no Ahoi navigation settings.
std::unique_ptr<views::View> CreateNavigationPinButton(PrefService* prefs);

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_SHELL_NAVIGATION_PIN_BUTTON_H_
