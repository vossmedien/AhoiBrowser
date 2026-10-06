// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/toast/ahoi_toast.h"

#include "chrome/browser/ui/toasts/api/toast_id.h"
#include "chrome/browser/ui/toasts/toast_controller.h"
#include "chrome/browser/ui/toasts/toast_view.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"

namespace ahoi::toast {

bool Show(BrowserWindowInterface* browser,
          Event event,
          const std::u16string& detail,
          size_t count) {
  ToastController* const controller =
      browser ? ToastController::From(browser) : nullptr;
  if (!controller) {
    return false;
  }
  // kCopiedToClipboard is registered unconditionally and carries no action
  // or menu; text and icon are fully overridden.
  ToastParams params(ToastId::kCopiedToClipboard);
  params.body_string_override = Message(event, detail, count);
  params.image_override =
      ui::ImageModel::FromVectorIcon(Icon(event), ui::kColorToastForeground,
                                     toasts::ToastView::GetIconSize());
  return controller->MaybeShowToast(std::move(params));
}

}  // namespace ahoi::toast
