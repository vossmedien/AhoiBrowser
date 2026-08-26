// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_CONTROLLER_H_
#define AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_CONTROLLER_H_

#include <memory>
#include <optional>

#include "ahoi/browser/privacy/privacy_mode_service.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Browser;

namespace content {
class WebContents;
}

namespace views {
class BubbleDialogDelegate;
class View;
class Widget;
}  // namespace views

namespace ahoi {

// Per-window coordinator for the profile-backed PrivacyModeService. The
// controller owns only transient Views objects; policy and exceptions remain
// profile data and the request throttle consumes immutable snapshots.
class PrivacyModeController {
 public:
  explicit PrivacyModeController(Browser* browser);
  PrivacyModeController(const PrivacyModeController&) = delete;
  PrivacyModeController& operator=(const PrivacyModeController&) = delete;
  ~PrivacyModeController();

  bool Show(views::View* anchor_view);
  bool CanShow() const;

 private:
  content::WebContents* GetActiveWebContents() const;
  bool SetGlobalMode(privacy::PrivacyMode mode);
  bool SetOriginMode(std::optional<privacy::PrivacyMode> mode);
  void ReloadActivePage();
  void OnBubbleClosed();

  raw_ptr<Browser> browser_ = nullptr;
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  std::unique_ptr<views::Widget> bubble_widget_;
  base::WeakPtrFactory<PrivacyModeController> weak_ptr_factory_{this};
};

}  // namespace ahoi

#endif  // AHOI_BROWSER_UI_PRIVACY_PRIVACY_MODE_CONTROLLER_H_
