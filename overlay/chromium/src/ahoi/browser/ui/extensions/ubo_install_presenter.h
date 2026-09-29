// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_PRESENTER_H_
#define AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_PRESENTER_H_

#include "ahoi/browser/extensions/ubo_service.h"

namespace ahoi::extensions {

enum class UboDialogAction {
  kNone,
  kBeginPinnedInstall,
  kDownloadUpdate,
  kInstallPreparedUpdate,
  kRemoveLite,
  kClose,
};

struct UboDialogPresentation {
  int status_string_id = 0;
  int primary_button_string_id = 0;
  UboDialogAction action = UboDialogAction::kNone;
  bool primary_enabled = false;
  bool show_progress = false;
  bool show_metadata = false;
};

// Pure mapping kept separate from Views so all user-visible secure/error
// states can be covered by a small unit test.
UboDialogPresentation PresentUboStatus(const UboServiceStatus& status);

}  // namespace ahoi::extensions

#endif  // AHOI_BROWSER_UI_EXTENSIONS_UBO_INSTALL_PRESENTER_H_
