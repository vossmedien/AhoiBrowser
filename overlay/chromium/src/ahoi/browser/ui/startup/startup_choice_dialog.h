// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_STARTUP_STARTUP_CHOICE_DIALOG_H_
#define AHOI_BROWSER_UI_STARTUP_STARTUP_CHOICE_DIALOG_H_

#include "base/functional/callback_forward.h"

class BrowserWindowInterface;

namespace views {
class Widget;
}

namespace ahoi::startup {

enum class StartupChoice {
  kContinue = 0,
  kEmpty = 1,
};

struct StartupChoiceResult {
  StartupChoice choice = StartupChoice::kEmpty;
  bool remember = false;
};

using StartupChoiceCallback =
    base::OnceCallback<void(StartupChoiceResult result)>;

// Shows a keyboard- and accessibility-native window-modal choice. Returning
// false means no dialog could be attached; callers must retain the already
// visible empty startup window as the fail-closed result.
[[nodiscard]] bool ShowStartupChoiceDialog(BrowserWindowInterface* browser,
                                           StartupChoiceCallback callback);

views::Widget* GetStartupChoiceDialogForTesting();
bool SetRememberStartupChoiceForTesting(bool remember);

}  // namespace ahoi::startup

#endif  // AHOI_BROWSER_UI_STARTUP_STARTUP_CHOICE_DIALOG_H_
